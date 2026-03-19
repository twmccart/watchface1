// PKJS companion for watchface1
// Fetches weather and sunrise/sunset from OpenWeatherMap and sends to the watch via AppMessage.

var OWM_API_KEY = 'e4db77e05017ec2320666f2e2465dcca'; // <-- replace with your key
var OWM_URL = 'https://api.openweathermap.org/data/2.5/weather';

// === TEST_MODE FLAG ===
// If the API key isn't set (placeholder), enable TEST_MODE. In TEST_MODE the
// companion sends a static, non-location payload so the watch UI and message
// handling can be tested without a real OpenWeatherMap key.
// Do not remove these TEST_MODE lines; they are intentionally left in place.
var TEST_MODE = (!OWM_API_KEY || OWM_API_KEY.indexOf('<') === 0);
// === END TEST_MODE FLAG ===
// Optional fixed coordinates - set to null to use geolocation. Useful for emulator.
var OWM_LAT = null; // e.g. 40.7128
var OWM_LON = null; // e.g. -74.0060

// Helper: send message to watch
function sendMessage(payload) {
  if (!Pebble || !Pebble.sendAppMessage) return;
  // Debug: log each key/value pair with details about sky glyph/icon
  for (var key in payload) {
    if (key == '10007') { // SKY_GLYPH
      console.log('SKY_GLYPH (10007): "' + payload[key] + '" (length=' + payload[key].length + ')');
    } else if (key == '10008') { // SKY_ICON  
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
  var url = OWM_URL + '?lat=' + coords.latitude + '&lon=' + coords.longitude + '&units=metric&appid=' + OWM_API_KEY;
  ajaxHelper(url, function(data) {
    try {
      var temp = Math.round(data.main.temp);
      var humidity = Math.round(data.main.humidity);
      var min = Math.round(data.main.temp_min);
      var max = Math.round(data.main.temp_max);
      var sunrise = data.sys.sunrise; // UNIX UTC
      var sunset = data.sys.sunset;
      // Safely extract the icon code from the OWM response. The JSON has
      // data.weather as an array; take weather[0].icon when available.
      var icon = (data.weather && data.weather[0] && data.weather[0].icon) ? data.weather[0].icon : null;
      // Map of OWM icon -> WeatherIcons glyph. Only use this mapping; if the
      // icon isn't present or not mapped, do not send a glyph.
      var iconToGlyph = {
        '01d': '', '02d': '', '03d': '', '04d': '', '09d': '', '10d': '', '11d': '', '13d': '', '50d': '',
        '01n': '', '02n': '', '03n': '', '04n': '', '09n': '', '10n': '', '11n': '', '13n': '', '50n': ''
      };
      var skyGlyph = (icon && iconToGlyph[icon]) ? iconToGlyph[icon] : null;
      var sendIconCode = icon ? icon : null;

      // Use numeric message keys to avoid mapping issues at runtime.
      var payload = {};
      payload[10000] = temp;      // WEATHER_TEMP
      payload[10001] = humidity;  // WEATHER_HUMIDITY
      payload[10002] = min;       // WEATHER_MIN
      payload[10003] = max;       // WEATHER_MAX
      payload[10004] = sunrise;   // SUNRISE
      payload[10005] = sunset;    // SUNSET
  // Send the sky glyph only if it was explicitly chosen from the OWM icon code.
  if (skyGlyph) payload[10007] = skyGlyph; // SKY_GLYPH (matches appinfo mapping)
  // Send the raw OWM icon code if available so the watch module can map it too.
  if (typeof sendIconCode !== 'undefined' && sendIconCode) payload[10008] = sendIconCode; // SKY_ICON
    // City name (if available)
    if (data.name) payload[10011] = data.name;
      sendMessage(payload);
    } catch (err) {
      console.log('Parse error: ' + err);
    }
  }, function(err) {
    console.log('Weather request failed: ' + err);
  });
}

function fetchAndSend() {
  if (!navigator.geolocation) {
    console.log('No geolocation');
    return;
  }
  // === TEST_MODE BRANCH (fetchAndSend) ===
  if (TEST_MODE) {
    console.log('TEST_MODE: sending static sunrise/sunset payload');
    // Static example: sunrise and sunset epoch values (UTC)
    var now = Math.floor(Date.now() / 1000);
    var payload = {};
    payload[10000] = 20;
    payload[10001] = 50;
    payload[10002] = 15;
    payload[10003] = 22;
    payload[10004] = now - 3600 * 6;
    payload[10005] = now + 3600 * 6;
    // Include a test OWM icon code and its mapped glyph so the watch displays it
    // in TEST_MODE (example: clear day -> '01d').
  payload[10008] = '01d';
  payload[10007] = '';
  payload[10011] = "Testville";
    sendMessage(payload);
    return;
  }
  // === END TEST_MODE BRANCH (fetchAndSend) ===
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
    // Include a test OWM icon code and glyph for ready/test mode as well.
  payload[10008] = '01d';
  payload[10007] = '';
  payload[10011] = "Testville";
    sendMessage(payload);
    return;
  }
  // === END TEST_MODE BRANCH (ready) ===

  // Not in TEST_MODE: attempt an immediate live fetch on ready so the watch
  // receives initial values promptly. Prefer fixed coords if present, else
  // fall back to geolocation (which will prompt the phone for permission).
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

  // On ready, also send current DARK_MODE setting if available from localStorage
  try {
    var dark = localStorage.getItem('dark_mode');
    if (dark !== null) {
      var dm = (dark === '1') ? 1 : 0;
      var payload = {};
      payload[10009] = dm; // DARK_MODE numeric key (10009)
      sendMessage(payload);
    }
  } catch (e) {
    console.log('No localStorage or error reading dark_mode: ' + e);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  console.log('AppMessage received: ' + JSON.stringify(e.payload));
  // Support request from watch to refresh
  // Some messages may use numeric keys (e.g., 100) to request a refresh
  if (e.payload && (e.payload.REQUEST_WEATHER || e.payload['100'])) {
    fetchAndSend();
  }
});

// Settings: show a tiny HTML page (data URL) with a checkbox for dark mode
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
  // e.response is the string after the # in the URL
  if (!e || !e.response) return;
  try {
    var data = JSON.parse(decodeURIComponent(e.response));
    if (data && data.D !== undefined) {
      var dm = (data.D === '1' || data.D === 1) ? 1 : 0;
      // Persist locally
      try { localStorage.setItem('dark_mode', dm ? '1' : '0'); } catch (ex) { }
      // Send to watch using the DARK_MODE message key numeric mapping
      var payload = {};
  payload[10009] = dm; // DARK_MODE numeric key
      sendMessage(payload);
    }
  } catch (err) {
    console.log('Config parse error: ' + err);
  }
});
