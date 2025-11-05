/* Watchface with complications:
   - Time (center)
   - Date (top)
   - Weather: current temp & humidity, min/max
   - Sunrise/sunset times
   - Bluetooth disconnect warning and battery <20% warning
   - Communicates with PKJS companion via AppMessage
*/

#include <pebble.h>
#include <stdlib.h>
// message_keys.auto.h is generated at build time from package.json messageKeys
#include "message_keys.auto.h"

// Forward declarations
static void prv_request_weather(void);

static Window *s_window;
static TextLayer *s_time_layer, *s_date_layer;
static TextLayer *s_weather_layer, *s_minmax_layer, *s_sun_layer, *s_status_layer;

// State
static int s_temp = 0;
static int s_humidity = 0;
static int s_min = 0;
static int s_max = 0;
static time_t s_sunrise = 0;
static time_t s_sunset = 0;
static bool s_bt_connected = true;
static int s_battery_level = 100;
static time_t s_last_weather_request = 0;
// cooldown in seconds (10 minutes)
static const int WEATHER_COOLDOWN = 10 * 60;
static bool s_prev_bt_connected = true;
// Per-layer persistent text buffers (TextLayer stores the pointer)
static char s_weather_buf[64];
static char s_minmax_buf[64];
static char s_sun_buf[64];
static char s_status_buf[32];
// Dark mode flag (user option to toggle later). true = black background, white text.
static bool s_dark_mode = true;

// Forward declaration to allow runtime toggle later
static void prv_set_dark_mode(bool enable);

enum {
  KEY_WEATHER_TEMP = 0,
  KEY_WEATHER_HUMIDITY = 1,
  KEY_WEATHER_MIN = 2,
  KEY_WEATHER_MAX = 3,
  KEY_SUNRISE = 4,
  KEY_SUNSET = 5,
  KEY_BT_CONNECTED = 6,
  KEY_BATTERY_LEVEL = 7,
  KEY_DATE_STRING = 8,
  KEY_REQUEST_WEATHER = 100
};

static void prv_update_time() {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  static char buf[8];
  if (clock_is_24h_style()) {
    strftime(buf, sizeof(buf), "%H:%M", tick_time);
  } else {
    strftime(buf, sizeof(buf), "%I:%M", tick_time);
    if (buf[0] == '0') memmove(buf, buf + 1, strlen(buf));
  }
  text_layer_set_text(s_time_layer, buf);

  // Date (ISO 8601)
  static char date_buf[32];
  strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tick_time);
  text_layer_set_text(s_date_layer, date_buf);
}

static void prv_format_and_update_weather() {
  // Weather line
  snprintf(s_weather_buf, sizeof(s_weather_buf), "Temp: %d°C  Hum: %d%%", s_temp, s_humidity);
  text_layer_set_text(s_weather_layer, s_weather_buf);

  // Min/Max line
  snprintf(s_minmax_buf, sizeof(s_minmax_buf), "Min:%d°C  Max:%d°C", s_min, s_max);
  text_layer_set_text(s_minmax_layer, s_minmax_buf);

  // Sunrise/Sunset line - always format placeholders so the layer shows something
  {
    char rbuf[16] = "--:--", sbuf[16] = "--:--";
    struct tm *tm;
    if (s_sunrise) {
      tm = localtime(&s_sunrise);
      if (tm) {
        strftime(rbuf, sizeof(rbuf), "%H:%M", tm);
      }
    }
    if (s_sunset) {
      tm = localtime(&s_sunset);
      if (tm) {
        strftime(sbuf, sizeof(sbuf), "%H:%M", tm);
      }
    }
    snprintf(s_sun_buf, sizeof(s_sun_buf), "Up %s  Down %s", rbuf, sbuf);
    text_layer_set_text(s_sun_layer, s_sun_buf);
  }

  // Status warnings
  if (!s_bt_connected) {
    strncpy(s_status_buf, "BT Disconnected", sizeof(s_status_buf));
    s_status_buf[sizeof(s_status_buf)-1] = '\0';
    text_layer_set_text(s_status_layer, s_status_buf);
  } else if (s_battery_level >= 0 && s_battery_level < 20) {
    snprintf(s_status_buf, sizeof(s_status_buf), "Low Battery: %d%%", s_battery_level);
    text_layer_set_text(s_status_layer, s_status_buf);
  } else {
    text_layer_set_text(s_status_layer, "");
  }
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  // Debug: log all tuples received so we can see keys/types/values
  Tuple *tt = dict_read_first(iter);
  while (tt) {
    if (tt->type == TUPLE_CSTRING) {
      APP_LOG(APP_LOG_LEVEL_INFO, "INBOX TUPLE key=%lu type=STRING val=%s", (unsigned long)tt->key, tt->value->cstring);
    } else {
      APP_LOG(APP_LOG_LEVEL_INFO, "INBOX TUPLE key=%lu type=%d int=%ld", (unsigned long)tt->key, (int)tt->type, (long)tt->value->int32);
    }
    tt = dict_read_next(iter);
  }

  Tuple *t;
  t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP);
  if (t) s_temp = (int)t->value->int32;
  t = dict_find(iter, MESSAGE_KEY_WEATHER_HUMIDITY);
  if (t) s_humidity = (int)t->value->int32;
  t = dict_find(iter, MESSAGE_KEY_WEATHER_MIN);
  if (t) s_min = (int)t->value->int32;
  t = dict_find(iter, MESSAGE_KEY_WEATHER_MAX);
  if (t) s_max = (int)t->value->int32;
  t = dict_find(iter, MESSAGE_KEY_SUNRISE);
  if (t) {
    if (t->type == TUPLE_CSTRING && t->value && t->value->cstring) {
      long v = strtol(t->value->cstring, NULL, 10);
      s_sunrise = (time_t)v;
    } else {
      s_sunrise = (time_t)t->value->int32;
    }
    APP_LOG(APP_LOG_LEVEL_INFO, "Received SUNRISE: %ld", (long)s_sunrise);
  }
  t = dict_find(iter, MESSAGE_KEY_SUNSET);
  if (t) {
    if (t->type == TUPLE_CSTRING && t->value && t->value->cstring) {
      long v = strtol(t->value->cstring, NULL, 10);
      s_sunset = (time_t)v;
    } else {
      s_sunset = (time_t)t->value->int32;
    }
    APP_LOG(APP_LOG_LEVEL_INFO, "Received SUNSET: %ld", (long)s_sunset);
  }
  // Optional: watch-initiated status
  t = dict_find(iter, MESSAGE_KEY_BT_CONNECTED);
  if (t) s_bt_connected = (bool)t->value->int32;
  t = dict_find(iter, MESSAGE_KEY_BATTERY_LEVEL);
  if (t) s_battery_level = (int)t->value->int32;

  prv_format_and_update_weather();
}

static void prv_inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "AppMessage dropped: %d", (int)reason);
}

static void prv_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)reason);
}

static void prv_outbox_sent(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox sent");
}

static void prv_bluetooth_callback(bool connected) {
  // Only trigger on transition disconnected -> connected
  bool was_connected = s_prev_bt_connected;
  s_prev_bt_connected = connected;
  s_bt_connected = connected;
  prv_format_and_update_weather();
  if (!was_connected && connected) {
    time_t now = time(NULL);
    if (now - s_last_weather_request >= WEATHER_COOLDOWN) {
      prv_request_weather();
      s_last_weather_request = now;
    } else {
      APP_LOG(APP_LOG_LEVEL_INFO, "Skipping weather request due to cooldown (%lds left)", (long)(WEATHER_COOLDOWN - (now - s_last_weather_request)));
    }
  }
}

static void prv_request_weather(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_int8(iter, 100, 1);
    dict_write_end(iter);
    app_message_outbox_send();
    s_last_weather_request = time(NULL);
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to begin outbox to request weather");
  }
}

static void prv_battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  prv_format_and_update_weather();
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time();
  // Every 30 minutes, request weather refresh
  if ((tick_time->tm_min % 20) == 0) {
    prv_request_weather();
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Make the watchface background black
  if (s_dark_mode) {
    window_set_background_color(window, GColorBlack);
  } else {
    window_set_background_color(window, GColorWhite);
  }

  s_date_layer = text_layer_create(GRect(0, 2, bounds.size.w, 20));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  s_time_layer = text_layer_create(GRect(0, 24, bounds.size.w, 40));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  s_weather_layer = text_layer_create(GRect(0, 68, bounds.size.w, 20));
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_weather_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_weather_layer));

  s_minmax_layer = text_layer_create(GRect(0, 88, bounds.size.w, 20));
  text_layer_set_background_color(s_minmax_layer, GColorClear);
  text_layer_set_text_color(s_minmax_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_minmax_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_minmax_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_minmax_layer));

  // Position sunrise/sunset at the very bottom, status line just above it
  const int SUN_HEIGHT = 20;
  const int STATUS_HEIGHT = 20;
  const int BOTTOM_MARGIN = 4;
  GRect sun_frame = GRect(0, bounds.size.h - SUN_HEIGHT - BOTTOM_MARGIN, bounds.size.w, SUN_HEIGHT);
  s_sun_layer = text_layer_create(sun_frame);
  text_layer_set_background_color(s_sun_layer, GColorClear);
  text_layer_set_text_color(s_sun_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_sun_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_sun_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_sun_layer));

  GRect status_frame = GRect(0, bounds.size.h - SUN_HEIGHT - STATUS_HEIGHT - BOTTOM_MARGIN - 2, bounds.size.w, STATUS_HEIGHT);
  s_status_layer = text_layer_create(status_frame);
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_text(s_status_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(s_status_layer));

  prv_update_time();
  prv_format_and_update_weather();
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_weather_layer);
  text_layer_destroy(s_minmax_layer);
  text_layer_destroy(s_sun_layer);
  text_layer_destroy(s_status_layer);
}

static void prv_init(void) {
  // Apply initial dark mode (can be toggled later)
  prv_set_dark_mode(s_dark_mode);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  // AppMessage
  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_register_outbox_failed(prv_outbox_failed);
  app_message_register_outbox_sent(prv_outbox_sent);
  const uint32_t inbox_size = 256;
  const uint32_t outbox_size = 256;
  app_message_open(inbox_size, outbox_size);

  // Tick, BT and battery
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  bluetooth_connection_service_subscribe(prv_bluetooth_callback);
  battery_state_service_subscribe(prv_battery_callback);

  // Initialize status: set previous BT state to the current state so we don't treat
  // the initial condition as a disconnected->connected transition.
  s_prev_bt_connected = bluetooth_connection_service_peek();
  prv_bluetooth_callback(s_prev_bt_connected);
  prv_battery_callback(battery_state_service_peek());

  // Request weather immediately on init
  prv_request_weather();
}

static void prv_set_dark_mode(bool enable) {
  s_dark_mode = enable;
  // If window exists update background and force a redraw by reloading window layers
  if (s_window) {
    // Update background color immediately
    window_set_background_color(s_window, s_dark_mode ? GColorBlack : GColorWhite);
    // Reformat and update text colors/content
    if (s_date_layer) text_layer_set_text_color(s_date_layer, s_dark_mode ? GColorWhite : GColorBlack);
    if (s_time_layer) text_layer_set_text_color(s_time_layer, s_dark_mode ? GColorWhite : GColorBlack);
    if (s_weather_layer) text_layer_set_text_color(s_weather_layer, s_dark_mode ? GColorWhite : GColorBlack);
    if (s_minmax_layer) text_layer_set_text_color(s_minmax_layer, s_dark_mode ? GColorWhite : GColorBlack);
    if (s_sun_layer) text_layer_set_text_color(s_sun_layer, s_dark_mode ? GColorWhite : GColorBlack);
    if (s_status_layer) text_layer_set_text_color(s_status_layer, s_dark_mode ? GColorWhite : GColorBlack);
  }
}

static void prv_deinit(void) {
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  APP_LOG(APP_LOG_LEVEL_INFO, "watchface1 initialized (diorite target)");
  app_event_loop();
  prv_deinit();
}
