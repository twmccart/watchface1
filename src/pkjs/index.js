// PKJS companion for watchface1
// Fetches weather from Open-Meteo (free, no API key required).
// Sends data to the watch via AppMessage using the same numeric message keys
// as before, so the watch-side C code needs no changes.

var OPEN_METEO_URL = 'https://api.open-meteo.com/v1/forecast';

// Optional fixed coordinates - set to null to use geolocation. Useful for emulator.
var OWM_LAT = null; // e.g. 40.7128
var OWM_LON = null; // e.g. -74.0060

// === TEST_MODE FLAG ===
// TEST_MODE is now triggered by a flag rather than a missing API key.
// Set to true to send a static payload without hitting the network.
var TEST_MODE = false;
// === END TEST_MODE FLAG ===

// Map WMO weather interpretation codes to OWM-style icon codes.
// This lets the watch-side C code keep using its existing icon resource lookup.
// Day/night variant is determined by the `is_day` field in the API response.
// WMO codes: https://open-meteo.com/en/docs#weathervariables
function wmoToOwmIcon(code, isDay) {
  var suffix = isDay ? 'd' : 'n';
  if (code === 0)                    return '01' + suffix; // Clear sky
  if (code === 1)                    return '02' + suffix; // Mainly clear
  if (code === 2)                    return '03' + suffix; // Partly cloudy
  if (code === 3)                    return '04' + suffix; // Overcast
  if (code === 45 || code === 48)    return '50' + suffix; // Fog / rime fog
  if (code === 51 || code === 53 || code === 55) return '09' + suffix; // Drizzle
  if (code === 56 || code === 57)    return '09' + suffix; // Freezing drizzle
  if (code === 61 || code === 63 || code === 65) return '10' + suffix; // Rain
  if (code === 66 || code === 67)    return '10' + suffix; // Freezing rain
  if (code >= 71 && code <= 77)      return '13' + suffix; // Snow
  if (code >= 80 && code <= 82)      return '10' + suffix; // Rain showers
  if (code === 85 || code === 86)    return '13' + suffix; // Snow showers
  if (code >= 95 && code <= 99)      return '11' + suffix; // Thunderstorm
  return '03' + suffix; // fallback: partly cloudy
}

// Map OWM icon code to a WeatherIcons font glyph (same mapping as before).
var iconToGlyph = {
  '01d': '\u{F00D}', '02d': '\u{F002}', '03d': '\u{F041}', '04d': '\u{F013}',
  '09d': '\u{F01A}', '10d': '\u{F019}', '11d': '\u{F01E}', '13d': '\u{F01B}', '50d': '\u{F014}',
  '01n': '\u{F02E}', '02n': '\u{F031}', '03n': '\u{F041}', '04n': '\u{F013}',
  '09n': '\u{F01A}', '10n': '\u{F028}', '11n': '\u{F01E}', '13n': '\u{F01B}', '50n': '\u{F014}'
};

// Helper: send message to watch
function sendMessage(payload) {
  if (!Pebble || !Pebble.sendAppMessage) return;
  for (var key in payload) {
    if (key == '10007') {
      console.log('SKY_GLYPH (10007): "' + payload[key] + '" (length=' + payload[key].length + ')');
    } else if (key == '10008') {
      console.log('SKY_ICON (10008): "' + payload[key] + '"');
    } else {
      console.log('Key ' + key + ': ' + payload[key]);
    }
  }
  Pebble.sendAppMessage(payload, function() {
    console.log('Send successful');
  }, function(e) {
    console.log('Send failed: ' + JSON.stringify(e));
  });
}

function ajaxHelper(url, cbSuccess, cbError) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    if (xhr.status >= 200 && xhr.status < 300) {
      try {
        var json = JSON.parse(xhr.responseText);
        cbSuccess(json);
      } catch (e) {
        cbError(e);
      }
    } else {
      cbError(new Error('HTTP status ' + xhr.status));
    }
  };
  xhr.onerror = function (e) { cbError(e); };
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
      var cur = data.current;
      var daily = data.daily;

      var temp     = Math.round(cur.temperature_2m);
      var humidity = Math.round(cur.relative_humidity_2m);
      var wmoCode  = cur.weather_code;
      var isDay    = cur.is_day === 1;

      // daily arrays are indexed by day; index 0 is today
      var min      = Math.round(daily.temperature_2m_min[0]);
      var max      = Math.round(daily.temperature_2m_max[0]);

      // Open-Meteo returns sunrise/sunset as ISO 8601 strings (local time).
      // Convert to Unix timestamps.
      var sunriseToday    = Math.floor(new Date(daily.sunrise[0]).getTime() / 1000);
      var sunriseTomorrow = Math.floor(new Date(daily.sunrise[1]).getTime() / 1000);
      var nowSec = Math.floor(Date.now() / 1000);
      // Show the next upcoming sunrise: today's if it hasn't happened yet, otherwise tomorrow's.
      var sunrise = (sunriseToday > nowSec) ? sunriseToday : sunriseTomorrow;
      var sunset   = Math.floor(new Date(daily.sunset[0]).getTime() / 1000);

      var iconCode = wmoToOwmIcon(wmoCode, isDay);
      var glyph    = iconToGlyph[iconCode] || null;

      var payload = {};
      payload[10000] = temp;
      payload[10001] = humidity;
      payload[10002] = min;
      payload[10003] = max;
      payload[10004] = sunrise;
      payload[10005] = sunset;
      payload[10008] = iconCode;
      if (glyph) payload[10007] = glyph;

      sendMessage(payload);
    } catch (err) {
      console.log('Parse error: ' + err);
    }
  }, function(err) {
    console.log('Weather request failed: ' + err);
  });
}

function fetchAndSend() {
  // === TEST_MODE BRANCH (fetchAndSend) ===
  if (TEST_MODE) {
    console.log('TEST_MODE: sending static payload');
    var now = Math.floor(Date.now() / 1000);
    var payload = {};
    payload[10000] = 20;
    payload[10001] = 50;
    payload[10002] = 15;
    payload[10003] = 22;
    payload[10004] = now - 3600 * 6;
    payload[10005] = now + 3600 * 6;
    payload[10008] = '01d';
    payload[10007] = iconToGlyph['01d'] || '';
    sendMessage(payload);
    return;
  }
  // === END TEST_MODE BRANCH ===

  if (!navigator.geolocation) {
    console.log('No geolocation');
    return;
  }
  navigator.geolocation.getCurrentPosition(function(pos) {
    fetchWeather(pos.coords);
  }, function(err) {
    console.log('Geoloc error: ' + err.message);
  }, {timeout: 10000});
}

Pebble.addEventListener('ready', function() {
  console.log('PKJS ready');

  // === TEST_MODE BRANCH (ready) ===
  if (TEST_MODE) {
    console.log('TEST_MODE: sending immediate static payload on ready');
    var now = Math.floor(Date.now() / 1000);
    var payload = {};
    payload[10000] = 20;
    payload[10001] = 50;
    payload[10002] = 15;
    payload[10003] = 22;
    payload[10004] = now - 3600 * 6;
    payload[10005] = now + 3600 * 6;
    payload[10008] = '01d';
    payload[10007] = iconToGlyph['01d'] || '';
    sendMessage(payload);
    return;
  }
  // === END TEST_MODE BRANCH ===

  // Attempt an immediate live fetch on ready.
  if (OWM_LAT !== null && OWM_LON !== null) {
    console.log('Using fixed coords for initial fetch: ' + OWM_LAT + ',' + OWM_LON);
    fetchWeather({ latitude: OWM_LAT, longitude: OWM_LON });
  } else if (navigator.geolocation) {
    console.log('Attempting geolocation for initial fetch');
    navigator.geolocation.getCurrentPosition(function(pos) {
      fetchWeather(pos.coords);
    }, function(err) {
      console.log('Geoloc error on ready: ' + err.message);
    }, {timeout: 10000});
  } else {
    console.log('No geolocation available and no fixed coords; initial fetch skipped');
  }

  // Send current DARK_MODE setting if available from localStorage
  try {
    var dark = localStorage.getItem('dark_mode');
    if (dark !== null) {
      var dm = (dark === '1') ? 1 : 0;
      var payload = {};
      payload[10009] = dm;
      sendMessage(payload);
    }
  } catch (e) {
    console.log('No localStorage or error reading dark_mode: ' + e);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  console.log('AppMessage received: ' + JSON.stringify(e.payload));
  if (e.payload && (e.payload.REQUEST_WEATHER || e.payload['100'])) {
    fetchAndSend();
  }
});

// Settings: show a tiny HTML page with a checkbox for dark mode
Pebble.addEventListener('showConfiguration', function() {
  try {
    var cur = localStorage.getItem('dark_mode') || '1';
    var checked = (cur === '1') ? 'checked' : '';
    var html = '' +
      '<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">' +
      '<style>body{font-family:sans-serif;padding:16px;} label{display:block;margin:12px 0;}</style>' +
      '</head><body>' +
      '<h3>watchface1 Settings</h3>' +
      '<label><input id="dark" type="checkbox" ' + checked + '> Dark mode (black background, white text)</label>' +
      '<button id="save">Save</button>' +
      '<script>' +
      'document.getElementById("save").addEventListener("click", function(){' +
      'var d = document.getElementById("dark").checked ? "1" : "0";' +
      'var result = { D: d };' +
      'var uri = "pebblejs://close#" + encodeURIComponent(JSON.stringify(result));' +
      'window.location = uri;' +
      '});' +
      '</script></body></html>';
    var url = 'data:text/html;base64,' + btoa(html);
    Pebble.openURL(url);
  } catch (e) {
    console.log('Failed to open config: ' + e);
  }
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  try {
    var data = JSON.parse(decodeURIComponent(e.response));
    if (data && data.D !== undefined) {
      var dm = (data.D === '1' || data.D === 1) ? 1 : 0;
      try { localStorage.setItem('dark_mode', dm ? '1' : '0'); } catch (ex) { }
      var payload = {};
      payload[10009] = dm;
      sendMessage(payload);
    }
  } catch (err) {
    console.log('Config parse error: ' + err);
  }
});
