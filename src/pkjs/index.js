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
  Pebble.sendAppMessage(payload, function() {
    console.log('Sent', JSON.stringify(payload));
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
      // Determine a compact sky condition code for the watch:
      // 0 = clear/sunny, 1 = clouds, 2 = rain/snow/precip
      var skyCond = 0;
      try {
        var w = data.weather && data.weather[0] && (data.weather[0].main || data.weather[0].id);
        if (w) {
          var wm = (typeof w === 'string') ? w.toLowerCase() : '' + w;
          if (wm.indexOf('cloud') !== -1) skyCond = 1;
          else if (wm.indexOf('rain') !== -1 || wm.indexOf('snow') !== -1 || wm.indexOf('drizzle') !== -1 || wm.indexOf('thunder') !== -1) skyCond = 2;
          else if (wm.indexOf('clear') !== -1) skyCond = 0;
        }
      } catch (e) { skyCond = 0; }

      // Use numeric message keys to avoid mapping issues at runtime.
      var payload = {};
      payload[10000] = temp;      // WEATHER_TEMP
      payload[10001] = humidity;  // WEATHER_HUMIDITY
      payload[10002] = min;       // WEATHER_MIN
      payload[10003] = max;       // WEATHER_MAX
      payload[10004] = sunrise;   // SUNRISE
      payload[10005] = sunset;    // SUNSET
      payload[10006] = skyCond;   // SKY_COND (compact code)
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
    // Include a test sky condition (0=clear)
    payload[10006] = 0;
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
    // Include a test sky condition (0 = clear)
    payload[10006] = 0;
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
