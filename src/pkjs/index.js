// PKJS companion for watchface1.
//
// Responsibilities:
//   - Fetch weather from Open-Meteo and send to watch via AppMessage.
//   - Host the phone-side settings UI via @rebble/clay (config.js).
//     Clay automatically handles showConfiguration and webviewclosed.
//     Do NOT add handlers for those events here.
//   - On watch reconnect (ready), resend stored settings via sendStoredSettings().
//
// Settings flow: user taps gear in Pebble app → Clay shows config.js UI →
// user saves → Clay sends all settings as one AppMessage and persists to
// localStorage → watch C code receives via chime_handle_inbox / prv_inbox_received.
//
// See scripts/build.sh — must be used instead of pebble build to avoid stale
// message key artifacts in the build directory.

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var OPEN_METEO_URL = 'https://api.open-meteo.com/v1/forecast';

// Optional fixed coordinates - set to null to use geolocation.
var OWM_LAT = null;
var OWM_LON = null;

// Set to true to send a static payload without hitting the network.
var TEST_MODE = false;

// Message key values — numeric IDs are assigned by position in the messageKeys
// array in package.json (starting at 10000). When adding or reordering keys
// there, update these constants to match, and delete build/js/message_keys.json
// before rebuilding so the SDK regenerates the JS-side Clay lookup table.
var KEY_WEATHER_TEMP    = 10000;
var KEY_WEATHER_HUMIDITY= 10001;
var KEY_WEATHER_MIN     = 10002;
var KEY_WEATHER_MAX     = 10003;
var KEY_SUNRISE         = 10004;
var KEY_SUNSET          = 10005;
var KEY_SKY_COND        = 10006;
var KEY_SKY_GLYPH       = 10007;
var KEY_SKY_ICON        = 10008;
var KEY_BT_CONNECTED    = 10009;
var KEY_BATTERY_LEVEL   = 10010;
var KEY_DATE_STRING     = 10011;
var KEY_DARK_MODE       = 10012;
var KEY_CITY            = 10013;
var KEY_VIBRATE_BT      = 10014;
var KEY_CHIME_ENABLED   = 10015;
var KEY_CHIME_VIBRATE   = 10016;
var KEY_QUIET_ENABLED   = 10017;
var KEY_QUIET_FROM        = 10018;
var KEY_QUIET_TO          = 10019;
var KEY_WEATHER_ON_SHAKE  = 10020;
var KEY_CHIME_ON_SHAKE    = 10021;
var KEY_CHIME_RESPECT_QT  = 10022;
var KEY_CHIME_VOLUME      = 10023;

// Map WMO weather codes to OWM-style icon codes.
function wmoToOwmIcon(code, isDay) {
  var suffix = isDay ? 'd' : 'n';
  if (code === 0)                    return '01' + suffix;
  if (code === 1)                    return '02' + suffix;
  if (code === 2)                    return '03' + suffix;
  if (code === 3)                    return '04' + suffix;
  if (code === 45 || code === 48)    return '50' + suffix;
  if (code === 51 || code === 53 || code === 55) return '09' + suffix;
  if (code === 56 || code === 57)    return '09' + suffix;
  if (code === 61 || code === 63 || code === 65) return '10' + suffix;
  if (code === 66 || code === 67)    return '10' + suffix;
  if (code >= 71 && code <= 77)      return '13' + suffix;
  if (code >= 80 && code <= 82)      return '10' + suffix;
  if (code === 85 || code === 86)    return '13' + suffix;
  if (code >= 95 && code <= 99)      return '11' + suffix;
  return '03' + suffix;
}

var iconToGlyph = {
  '01d': '\u{F00D}', '02d': '\u{F002}', '03d': '\u{F041}', '04d': '\u{F013}',
  '09d': '\u{F01A}', '10d': '\u{F019}', '11d': '\u{F01E}', '13d': '\u{F01B}', '50d': '\u{F014}',
  '01n': '\u{F02E}', '02n': '\u{F031}', '03n': '\u{F041}', '04n': '\u{F013}',
  '09n': '\u{F01A}', '10n': '\u{F028}', '11n': '\u{F01E}', '13n': '\u{F01B}', '50n': '\u{F014}'
};

function sendMessage(payload) {
  if (!Pebble || !Pebble.sendAppMessage) return;
  Pebble.sendAppMessage(payload,
    function() { console.log('Send successful'); },
    function(e) { console.log('Send failed: ' + JSON.stringify(e)); }
  );
}

function ajaxHelper(url, cbSuccess, cbError) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    if (xhr.status >= 200 && xhr.status < 300) {
      try { cbSuccess(JSON.parse(xhr.responseText)); }
      catch (e) { cbError(e); }
    } else {
      cbError(new Error('HTTP status ' + xhr.status));
    }
  };
  xhr.onerror = function(e) { cbError(e); };
  xhr.open('GET', url);
  xhr.send();
}

function fetchWeather(coords) {
  var url = OPEN_METEO_URL +
    '?latitude=' + coords.latitude +
    '&longitude=' + coords.longitude +
    '&current=temperature_2m,relative_humidity_2m,weather_code,is_day' +
    '&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset' +
    '&temperature_unit=celsius' +
    '&timezone=auto';

  ajaxHelper(url, function(data) {
    try {
      var cur   = data.current;
      var daily = data.daily;
      var temp     = Math.round(cur.temperature_2m);
      var humidity = Math.round(cur.relative_humidity_2m);
      var isDay    = cur.is_day === 1;
      var min      = Math.round(daily.temperature_2m_min[0]);
      var max      = Math.round(daily.temperature_2m_max[0]);

      var nowSec          = Math.floor(Date.now() / 1000);
      var sunriseToday    = Math.floor(new Date(daily.sunrise[0]).getTime() / 1000);
      var sunriseTomorrow = Math.floor(new Date(daily.sunrise[1]).getTime() / 1000);
      var sunrise = (sunriseToday > nowSec) ? sunriseToday : sunriseTomorrow;
      var sunset  = Math.floor(new Date(daily.sunset[0]).getTime() / 1000);

      var iconCode = wmoToOwmIcon(cur.weather_code, isDay);
      var glyph    = iconToGlyph[iconCode] || null;

      var payload = {};
      payload[KEY_WEATHER_TEMP]     = temp;
      payload[KEY_WEATHER_HUMIDITY] = humidity;
      payload[KEY_WEATHER_MIN]      = min;
      payload[KEY_WEATHER_MAX]      = max;
      payload[KEY_SUNRISE]          = sunrise;
      payload[KEY_SUNSET]           = sunset;
      payload[KEY_SKY_ICON]         = iconCode;
      if (glyph) payload[KEY_SKY_GLYPH] = glyph;

      sendMessage(payload);
    } catch (err) {
      console.log('Parse error: ' + err);
    }
  }, function(err) {
    console.log('Weather request failed: ' + err);
  });
}

function fetchAndSend() {
  if (TEST_MODE) {
    var now = Math.floor(Date.now() / 1000);
    var p = {};
    p[KEY_WEATHER_TEMP]     = 20;
    p[KEY_WEATHER_HUMIDITY] = 50;
    p[KEY_WEATHER_MIN]      = 15;
    p[KEY_WEATHER_MAX]      = 22;
    p[KEY_SUNRISE]          = now - 3600 * 6;
    p[KEY_SUNSET]           = now + 3600 * 6;
    p[KEY_SKY_ICON]         = '01d';
    p[KEY_SKY_GLYPH]        = iconToGlyph['01d'] || '';
    sendMessage(p);
    return;
  }
  if (!navigator.geolocation) { console.log('No geolocation'); return; }
  navigator.geolocation.getCurrentPosition(function(pos) {
    fetchWeather(pos.coords);
  }, function(err) {
    console.log('Geoloc error: ' + err.message);
  }, {timeout: 10000});
}

// Send Clay's stored settings to the watch (used on reconnect).
// Clay handles showConfiguration and webviewclosed automatically.
function sendStoredSettings() {
  var s = clay.getSettings();
  if (!s || Object.keys(s).length === 0) return;
  var payload = {};
  payload[KEY_DARK_MODE]        = s.DARK_MODE        ? 1 : 0;
  payload[KEY_CHIME_ENABLED]    = s.CHIME_ENABLED    !== false ? 1 : 0;
  payload[KEY_CHIME_VIBRATE]    = s.CHIME_VIBRATE    ? 1 : 0;
  payload[KEY_QUIET_ENABLED]    = s.QUIET_ENABLED    ? 1 : 0;
  payload[KEY_QUIET_FROM]       = s.QUIET_FROM       !== undefined ? parseInt(s.QUIET_FROM,  10) : 22;
  payload[KEY_QUIET_TO]         = s.QUIET_TO         !== undefined ? parseInt(s.QUIET_TO,    10) : 7;
  payload[KEY_WEATHER_ON_SHAKE] = s.WEATHER_ON_SHAKE ? 1 : 0;
  payload[KEY_CHIME_ON_SHAKE]   = s.CHIME_ON_SHAKE   ? 1 : 0;
  payload[KEY_CHIME_RESPECT_QT] = s.CHIME_RESPECT_QT ? 1 : 0;
  payload[KEY_CHIME_VOLUME]     = s.CHIME_VOLUME     !== undefined ? parseInt(s.CHIME_VOLUME, 10) : 100;
  sendMessage(payload);
}

Pebble.addEventListener('ready', function() {
  console.log('PKJS ready');
  if (TEST_MODE) { fetchAndSend(); return; }
  if (OWM_LAT !== null && OWM_LON !== null) {
    fetchWeather({ latitude: OWM_LAT, longitude: OWM_LON });
  } else if (navigator.geolocation) {
    navigator.geolocation.getCurrentPosition(function(pos) {
      fetchWeather(pos.coords);
    }, function(err) {
      console.log('Geoloc error on ready: ' + err.message);
    }, {timeout: 10000});
  }
  sendStoredSettings();
});

Pebble.addEventListener('appmessage', function(e) {
  console.log('AppMessage received: ' + JSON.stringify(e.payload));
  if (e.payload && (e.payload.REQUEST_WEATHER || e.payload['100'])) {
    fetchAndSend();
  }
});
