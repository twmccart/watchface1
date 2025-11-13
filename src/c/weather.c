#include "weather.h"
#include "message_keys.auto.h"
// Fallback for SKY_GLYPH message key if generated header isn't up-to-date.
#ifndef MESSAGE_KEY_SKY_GLYPH
#define MESSAGE_KEY_SKY_GLYPH 10007
#endif
#ifndef MESSAGE_KEY_SKY_ICON
#define MESSAGE_KEY_SKY_ICON 10008
#endif

/* C-side glyph mapping removed - companion handles all mapping */

/* Internal state */
static weather_data_t s_data = {0};
static weather_update_callback s_callback = NULL;
static void *s_callback_ctx = NULL;

/* Forward declarations for functions used before their definitions */
static void weather_bt_handler(bool connected);
static void schedule_weather_retry(void);
static void cancel_weather_retry(void);

void weather_init(weather_update_callback cb, void *ctx) {
  s_callback = cb;
  s_callback_ctx = ctx;
  memset(&s_data, 0, sizeof(s_data));
  // Subscribe to BT events so retries can resume on reconnect
  bluetooth_connection_service_subscribe(weather_bt_handler);
}

void weather_deinit(void) {
  s_callback = NULL;
  s_callback_ctx = NULL;
  // Cancel any pending retry and unsubscribe from BT
  cancel_weather_retry();
  bluetooth_connection_service_unsubscribe();
}

const weather_data_t *weather_get(void) {
  return &s_data;
}

static void notify_if_needed(void) {
  // Per design: do NOT synthesize a glyph from the icon_code. Only use
  // an explicit glyph provided by the companion (MESSAGE_KEY_SKY_GLYPH).
  // If no glyph was provided, leave s_data.glyph empty so the UI can
  // decide to hide glyphs. This matches the user's instruction to rely
  // solely on the OWM icon string/glyph provided by the companion.
  APP_LOG(APP_LOG_LEVEL_INFO, "notify_if_needed: glyph='%s' (len=%d) icon='%s' temp=%d", 
          s_data.glyph, (int)strlen(s_data.glyph), s_data.icon_code, s_data.temp);
  if (s_callback) s_callback(&s_data, s_callback_ctx);
}

void weather_handle_inbox(DictionaryIterator *iter) {
  bool changed = false;
  Tuple *t;
  t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP);
  if (t) { int v = (int)t->value->int32; if (v != s_data.temp) { s_data.temp = v; changed = true; } }
  t = dict_find(iter, MESSAGE_KEY_WEATHER_HUMIDITY);
  if (t) { int v = (int)t->value->int32; if (v != s_data.humidity) { s_data.humidity = v; changed = true; } }
  t = dict_find(iter, MESSAGE_KEY_WEATHER_MIN);
  if (t) { int v = (int)t->value->int32; if (v != s_data.min) { s_data.min = v; changed = true; } }
  t = dict_find(iter, MESSAGE_KEY_WEATHER_MAX);
  if (t) { int v = (int)t->value->int32; if (v != s_data.max) { s_data.max = v; changed = true; } }
  t = dict_find(iter, MESSAGE_KEY_SUNRISE);
  if (t) {
    time_t val = 0;
    if (t->type == TUPLE_CSTRING && t->value && t->value->cstring) val = (time_t)strtol(t->value->cstring, NULL, 10);
    else val = (time_t)t->value->int32;
    if (val != s_data.sunrise) { s_data.sunrise = val; changed = true; }
  }
  t = dict_find(iter, MESSAGE_KEY_SUNSET);
  if (t) {
    time_t val = 0;
    if (t->type == TUPLE_CSTRING && t->value && t->value->cstring) val = (time_t)strtol(t->value->cstring, NULL, 10);
    else val = (time_t)t->value->int32;
    if (val != s_data.sunset) { s_data.sunset = val; changed = true; }
  }
  t = dict_find(iter, MESSAGE_KEY_SKY_COND);
  if (t) {
    int sc = 0;
    if (t->type == TUPLE_CSTRING && t->value && t->value->cstring) sc = atoi(t->value->cstring);
    else sc = (int)t->value->int32;
    if (sc != s_data.sky_code) { s_data.sky_code = sc; changed = true; }
  }
  t = dict_find(iter, MESSAGE_KEY_CITY);
  if (t && t->type == TUPLE_CSTRING && t->value && t->value->cstring) {
    if (strncmp(s_data.city, t->value->cstring, sizeof(s_data.city)) != 0) {
      strncpy(s_data.city, t->value->cstring, sizeof(s_data.city));
      s_data.city[sizeof(s_data.city)-1] = '\0';
      changed = true;
    }
  }
  t = dict_find(iter, MESSAGE_KEY_SKY_GLYPH);
  if (t && t->type == TUPLE_CSTRING && t->value && t->value->cstring) {
    if (strncmp(s_data.glyph, t->value->cstring, sizeof(s_data.glyph)) != 0) {
      strncpy(s_data.glyph, t->value->cstring, sizeof(s_data.glyph));
      s_data.glyph[sizeof(s_data.glyph)-1] = '\0';
      APP_LOG(APP_LOG_LEVEL_INFO, "Received SKY_GLYPH: '%s' (len=%d)", s_data.glyph, (int)strlen(s_data.glyph));
      changed = true;
    }
  }
  t = dict_find(iter, MESSAGE_KEY_SKY_ICON);
  if (t && t->type == TUPLE_CSTRING && t->value && t->value->cstring) {
    if (strncmp(s_data.icon_code, t->value->cstring, sizeof(s_data.icon_code)) != 0) {
      strncpy(s_data.icon_code, t->value->cstring, sizeof(s_data.icon_code));
      s_data.icon_code[sizeof(s_data.icon_code)-1] = '\0';
      APP_LOG(APP_LOG_LEVEL_INFO, "Received SKY_ICON: '%s'", s_data.icon_code);
      changed = true;
    }
  }

  if (changed) notify_if_needed();
}

/* Cooldown management for requests (seconds). Default 10 minutes. */
static const int WEATHER_COOLDOWN = 10 * 60;
static time_t s_last_request = 0;

/* Forward declare retry helper functions */
static void schedule_weather_retry(void);
static void cancel_weather_retry(void);

bool weather_request(void) {
  time_t now = time(NULL);
  if (now - s_last_request < WEATHER_COOLDOWN) {
    return false;
  }
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_int8(iter, 100, 1);
    dict_write_end(iter);
    AppMessageResult res = app_message_outbox_send();
    if (res == APP_MSG_OK) {
      s_last_request = now;
      // If there was a pending retry, clear it
      return true;
    } else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "app_message_outbox_send failed: %d", (int)res);
      // fall through to schedule retry
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "app_message_outbox_begin failed (weather_request)");
  }
  // If we get here, sending failed; schedule a retry
  // (retry state managed below)
  schedule_weather_retry();
  return false;
}

void weather_force_request(void) {
  // Try immediate send even if cooldown active. Cancel any pending retries
  // and attempt send now.
  cancel_weather_retry();
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_int8(iter, 100, 1);
    dict_write_end(iter);
    AppMessageResult res = app_message_outbox_send();
    if (res == APP_MSG_OK) {
      s_last_request = time(NULL);
    } else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "app_message_outbox_send failed (force): %d", (int)res);
      schedule_weather_retry();
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "app_message_outbox_begin failed (force)");
    schedule_weather_retry();
  }
}

/* Retry scheduling */
/* Retry scheduling with exponential backoff and indefinite deferral while
   Bluetooth is disconnected. */
static AppTimer *s_retry_timer = NULL;
static int s_retry_count = 0; /* number of attempts already made */
static const int WEATHER_RETRY_BASE = 30; /* base interval in seconds */
static const int WEATHER_RETRY_MAX_INTERVAL = 10 * 60; /* cap to 10 minutes */
static const int WEATHER_MAX_RETRIES = 8; /* upper attempt cap when connected */
static bool s_pending_request = false; /* true when a request needs to be sent */

static void cancel_weather_retry(void) {
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  s_retry_count = 0;
  s_pending_request = false;
}

/* Compute backoff interval (seconds) for attempt number n (1-based).
   Uses exponential backoff: base * 2^(n-1), capped at max interval. */
static int backoff_interval_seconds(int attempt) {
  if (attempt <= 0) attempt = 1;
  /* Be careful shifting: attempt-1 could be large, but we cap result. */
  long interval = (long)WEATHER_RETRY_BASE << (attempt - 1);
  if (interval > WEATHER_RETRY_MAX_INTERVAL) interval = WEATHER_RETRY_MAX_INTERVAL;
  return (int)interval;
}

static void retry_timer_cb(void *data) {
  s_retry_timer = NULL;
  /* If bluetooth disconnected, keep the pending flag and do not consume an
     attempt. Defer until reconnect without incrementing s_retry_count. */
  if (!bluetooth_connection_service_peek()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "BT disconnected, deferring weather retry (indefinite)");
    s_pending_request = true;
    return;
  }

  /* Attempt to send again */
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_int8(iter, 100, 1);
    dict_write_end(iter);
    AppMessageResult res = app_message_outbox_send();
    if (res == APP_MSG_OK) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Weather retry sent successfully");
      s_last_request = time(NULL);
      s_pending_request = false;
      s_retry_count = 0;
      return;
    } else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Retry send failed: %d", (int)res);
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "app_message_outbox_begin failed during retry");
  }

  /* Sending failed while connected: increment attempt count and schedule next
     retry using exponential backoff, up to WEATHER_MAX_RETRIES. */
  s_retry_count++;
  if (s_retry_count <= WEATHER_MAX_RETRIES) {
    int interval = backoff_interval_seconds(s_retry_count);
    APP_LOG(APP_LOG_LEVEL_INFO, "Scheduling weather retry #%d in %d seconds", s_retry_count, interval);
    s_retry_timer = app_timer_register(interval * 1000, retry_timer_cb, NULL);
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Max weather retries reached; giving up until next trigger");
    s_pending_request = false;
    s_retry_count = 0;
  }
}

static void schedule_weather_retry(void) {
  /* Mark that a send is pending. If BT is disconnected, we just keep the
     pending flag (indefinite defer). If BT is connected and no timer is
     active, begin an exponential-backoff retry sequence starting at attempt 1. */
  s_pending_request = true;
  if (!bluetooth_connection_service_peek()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "schedule_weather_retry: BT down, deferring indefinitely");
    return;
  }
  if (s_retry_timer) return;
  s_retry_count = 1;
  int interval = backoff_interval_seconds(s_retry_count);
  APP_LOG(APP_LOG_LEVEL_INFO, "Scheduling weather retry #1 in %d seconds", interval);
  s_retry_timer = app_timer_register(interval * 1000, retry_timer_cb, NULL);
}

/* Bluetooth callback: when we reconnect, attempt an immediate retry/send
   if a pending request was waiting. */
static void weather_bt_handler(bool connected) {
  if (connected && s_pending_request) {
    // Attempt immediate send on reconnect. If it fails, schedule exponential-backoff retries.
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_int8(iter, 100, 1);
      dict_write_end(iter);
      AppMessageResult res = app_message_outbox_send();
      if (res == APP_MSG_OK) {
        APP_LOG(APP_LOG_LEVEL_INFO, "Weather send succeeded on BT reconnect");
        s_last_request = time(NULL);
        s_pending_request = false;
        s_retry_count = 0;
        if (s_retry_timer) {
          app_timer_cancel(s_retry_timer);
          s_retry_timer = NULL;
        }
        return;
      } else {
        APP_LOG(APP_LOG_LEVEL_WARNING, "Weather send failed on reconnect: %d", (int)res);
        /* fall through to schedule exponential retry */
      }
    } else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "app_message_outbox_begin failed on BT reconnect");
    }
    // Start retry sequence (will check BT again before attempting)
    schedule_weather_retry();
  }
}

/* Periodic polling state */
static uint16_t s_periodic_interval_minutes = 0;
static bool s_periodic_enabled = false;

static void weather_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (!s_periodic_enabled || s_periodic_interval_minutes == 0) return;
  if ((tick_time->tm_min % s_periodic_interval_minutes) == 0) {
    // Use the module's request function which enforces cooldown
    if (!weather_request()) {
      APP_LOG(APP_LOG_LEVEL_INFO, "weather_request skipped by cooldown (periodic)");
    }
  }
}

void weather_start_periodic(uint16_t minutes) {
  if (minutes == 0) return;
  s_periodic_interval_minutes = minutes;
  if (!s_periodic_enabled) {
    tick_timer_service_subscribe(MINUTE_UNIT, weather_tick_handler);
    s_periodic_enabled = true;
  }
  // Immediately attempt a forced request so UI gets fresh data
  weather_force_request();
}

void weather_stop_periodic(void) {
  if (s_periodic_enabled) {
    tick_timer_service_unsubscribe();
    s_periodic_enabled = false;
    s_periodic_interval_minutes = 0;
  }
}

bool weather_is_periodic_enabled(void) {
  return s_periodic_enabled;
}

void weather_run_sample_test(void) {
  s_data.temp = 21;
  s_data.humidity = 58;
  s_data.min = 15;
  s_data.max = 24;
  s_data.sunrise = time(NULL) - 3600; // 1 hour ago
  s_data.sunset = time(NULL) + 3600 * 10; // in 10 hours
  s_data.sky_code = 1;
  strncpy(s_data.city, "Testville", sizeof(s_data.city));
  s_data.city[sizeof(s_data.city)-1] = '\0';
  strncpy(s_data.icon_code, "01d", sizeof(s_data.icon_code));
  s_data.icon_code[sizeof(s_data.icon_code)-1] = '\0';
  /* For emulator/test mode also populate a sample glyph so the UI shows
     both the raw icon string and a glyph (mimics companion-provided glyph). */
  strncpy(s_data.glyph, "", sizeof(s_data.glyph));
  s_data.glyph[sizeof(s_data.glyph)-1] = '\0';
  notify_if_needed();
}

/* Map OWM icon code string to a default glyph. This provides a place for the
   user to customize glyphs for each OWM icon. Returns non-empty glyph string
   if mapping exists, otherwise empty string. */
static const char *map_icon_code_to_glyph(const char *icon_code) {
  if (!icon_code || !icon_code[0]) return "";
  /* Known OWM icon codes to support:
     01d,02d,03d,04d,09d,10d,11d,13d,50d,
     01n,02n,03n,04n,09n,10n,11n,13n,50n

     Fill the `glyph` field in the table below with the WeatherIcons glyph
     (UTF-8 string) you want for each code. Leave as "" to use procedural
     drawing fallback or the companion-provided glyph.
  */
  static const struct { const char *code; const char *glyph; } map[] = {
    { "01d", "" }, // clear sky day 
    { "02d", "" }, // few clouds day 
    { "03d", "" }, // scattered clouds day
    { "04d", "" }, // broken clouds day
    { "09d", "" }, // shower rain day 
    { "10d", "" }, // rain day 
    { "11d", "" }, // thunderstorm day 
    { "13d", "" }, // snow day 
    { "50d", "" }, // mist day 
    { "01n", "" }, // clear sky night 
    { "02n", "" }, // few clouds night 
    { "03n", "" }, // scattered clouds night 
    { "04n", "" }, // broken clouds night
    { "09n", "" }, // shower rain night 
    { "10n", "" }, // rain night 
    { "11n", "" }, // thunderstorm night 
    { "13n", "" }, // snow night 
    { "50n", "" }, // mist night 
    { NULL, NULL }
  };

  for (int i = 0; map[i].code; ++i) {
    if (strcmp(icon_code, map[i].code) == 0) return map[i].glyph;
  }
  return "";
}
