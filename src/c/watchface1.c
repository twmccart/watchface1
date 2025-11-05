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

// If build didn't regenerate message_keys header for DARK_MODE yet, provide a
// fallback numeric value matching appinfo.json (will be 10009 after package.json change).
#ifndef MESSAGE_KEY_DARK_MODE
#define MESSAGE_KEY_DARK_MODE 10009
#endif

// Fallback for SKY_COND (numeric key generated into appinfo.json). If the
// generated header isn't up-to-date during a build, define it here.
#ifndef MESSAGE_KEY_SKY_COND
#define MESSAGE_KEY_SKY_COND 10006
#endif

// Forward declarations
static void prv_request_weather(void);

static Window *s_window;
static TextLayer *s_time_layer, *s_date_layer;
static TextLayer *s_temp_layer, *s_hum_layer, *s_minmax_layer, *s_sunrise_layer, *s_sunset_layer, *s_status_layer;
static Layer *s_sky_layer;
static bool s_sky_test_all = false; // when true, draw all three icons for testing

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
// static char s_weather_buf[64]; // no longer used; replaced by s_temp_buf and s_hum_buf
static char s_temp_buf[32];
static char s_hum_buf[32];
static char s_minmax_buf[64];
static char s_sunrise_buf[32];
static char s_sunset_buf[32];
static char s_status_buf[32];
static int s_sky_code = 0; // 0=sun,1=cloud,2=precip
// Dark mode flag (user option to toggle later). true = black background, white text.
static bool s_dark_mode = true;

// Forward declaration to allow runtime toggle later
static void prv_set_dark_mode(bool enable);

// Persistent storage keys
#define PERSIST_KEY_DARK_MODE 1

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
  // Show compact temperature and humidity near the top-left icon (no labels)
  snprintf(s_temp_buf, sizeof(s_temp_buf), "%d°C", s_temp);
  text_layer_set_text(s_temp_layer, s_temp_buf);
  snprintf(s_hum_buf, sizeof(s_hum_buf), "%d%%", s_humidity);
  text_layer_set_text(s_hum_layer, s_hum_buf);
  // Compute width of temp text and place humidity one space after it
  GFont temp_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  GRect measure_box = GRect(0, 0, 128, 24);
  GSize temp_size = graphics_text_layout_get_content_size(s_temp_buf, temp_font, measure_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize space_size = graphics_text_layout_get_content_size(" ", temp_font, measure_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  // temp is positioned at x = 2 (sky_x) + 16 (ICON_SIZE) + 4 padding = 20
  int hum_x = 24 + (int)(temp_size.w + space_size.w);
  // Move humidity layer to calculated X
  Layer *hum_layer = text_layer_get_layer(s_hum_layer);
  GRect hum_frame = layer_get_frame(hum_layer);
  hum_frame.origin.x = hum_x;
  layer_set_frame(hum_layer, hum_frame);

  // Min/Max line
  // Show min/max compactly in upper-right as "min-max°"
  snprintf(s_minmax_buf, sizeof(s_minmax_buf), "%d-%d°", s_min, s_max);
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
  // Use small arrow glyphs for sunrise (↑) and sunset (↓). If target font
  // doesn't contain these glyphs, they'll fall back to a placeholder.
  snprintf(s_sunrise_buf, sizeof(s_sunrise_buf), "%s", rbuf);
  snprintf(s_sunset_buf, sizeof(s_sunset_buf), "%s", sbuf);
  text_layer_set_text(s_sunrise_layer, s_sunrise_buf);
  text_layer_set_text(s_sunset_layer, s_sunset_buf);
  }

  // Status warnings
  // Read live BT state to avoid stale values
  bool live_bt = bluetooth_connection_service_peek();
  APP_LOG(APP_LOG_LEVEL_INFO, "BT peek=%d s_bt_connected=%d", (int)live_bt, (int)s_bt_connected);
  if (!live_bt) {
    strncpy(s_status_buf, "BT Disconnected", sizeof(s_status_buf));
    s_status_buf[sizeof(s_status_buf)-1] = '\0';
    text_layer_set_text(s_status_layer, s_status_buf);
  } else if (s_battery_level >= 0 && s_battery_level < 20) {
    snprintf(s_status_buf, sizeof(s_status_buf), "Low Battery: %d%%", s_battery_level);
    text_layer_set_text(s_status_layer, s_status_buf);
  } else {
    text_layer_set_text(s_status_layer, "Status OK");
    text_layer_set_text_color(s_status_layer, s_dark_mode ? GColorWhite : GColorBlack);
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

  // SKY_COND may be sent as numeric code (0=clear,1=clouds,2=precip)
  t = dict_find(iter, MESSAGE_KEY_SKY_COND);
  if (t) {
    int sc = 0;
    if (t->type == TUPLE_CSTRING && t->value && t->value->cstring) sc = atoi(t->value->cstring);
    else sc = (int)t->value->int32;
    s_sky_code = sc;
    if (s_sky_layer) layer_mark_dirty(s_sky_layer);
  }

  // DARK_MODE may come as an int or string; if present, persist and apply
  t = dict_find(iter, MESSAGE_KEY_DARK_MODE);
  if (t) {
    int dm = 0;
    if (t->type == TUPLE_CSTRING && t->value && t->value->cstring) {
      dm = atoi(t->value->cstring);
    } else {
      dm = (int)t->value->int32;
    }
    prv_set_dark_mode(dm ? true : false);
    // Persist the choice so it survives restarts
    persist_write_int(PERSIST_KEY_DARK_MODE, dm);
    APP_LOG(APP_LOG_LEVEL_INFO, "DARK_MODE set to %d", dm);
  }

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

// Draw a small filled icon for the sky condition in the top-left.
static void sky_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, s_dark_mode ? GColorWhite : GColorBlack);
  const int ICON_SIZE = 16;
  const int SPACING = 4;
  const int CLOUD_Y_OFFSET = -1; // raise cloud vertically so it sits higher (reduced to lower by 2px)

  // If testing flag is set, draw three icons left-to-right centered in layer
  if (s_sky_test_all) {
    int total_w = 3 * ICON_SIZE + 2 * SPACING;
    int left = (bounds.size.w - total_w) / 2;
    for (int i = 0; i < 3; i++) {
      GRect icon = GRect(left + i * (ICON_SIZE + SPACING), 0, ICON_SIZE, ICON_SIZE);
      GPoint c = GPoint(icon.origin.x + ICON_SIZE/2, icon.origin.y + ICON_SIZE/2);
      if (i == 0) {
        // Sun
        graphics_fill_circle(ctx, c, 5);
        for (int r = 0; r < 8; r++) {
          int angle = r * 45;
          int16_t sx = c.x + (int16_t)(sin_lookup(TRIG_MAX_ANGLE * angle / 360) * 7 / TRIG_MAX_RATIO);
          int16_t sy = c.y - (int16_t)(cos_lookup(TRIG_MAX_ANGLE * angle / 360) * 7 / TRIG_MAX_RATIO);
          graphics_fill_circle(ctx, GPoint(sx, sy), 1);
        }
      } else if (i == 1) {
  // Cloud (lowered slightly)
  GPoint p1 = GPoint(c.x - 3, c.y + 1 + CLOUD_Y_OFFSET);
  GPoint p2 = GPoint(c.x + 3, c.y + 1 + CLOUD_Y_OFFSET);
  graphics_fill_circle(ctx, p1, 4);
  graphics_fill_circle(ctx, p2, 4);
  graphics_fill_rect(ctx, GRect(c.x - 8, c.y + 1 + CLOUD_Y_OFFSET, 16, 6), 2, GCornersAll);
      } else {
        // Rain: teardrop oriented with round bottom and triangular top
        int dx = c.x;
        int dy_bot = c.y + 3; // lift the drop by 2 pixels
        // rounded bottom (larger radius for rounder bottom)
        int bottom_radius = 4.5;
        graphics_fill_circle(ctx, GPoint(dx, dy_bot), bottom_radius);
        // triangular top using GPath (shorter and wider) placed just above circle
        {
          int base_y = dy_bot - bottom_radius - 1; // base just above circle
          GPathInfo tri_info = {3, (GPoint[]){{0, -5}, {-3, 0}, {3, 0}}};
          GPath *tri = gpath_create(&tri_info);
          gpath_move_to(tri, GPoint(dx, base_y));
          gpath_draw_filled(ctx, tri);
          gpath_destroy(tri);
        }
      }
    }
  } else {
    // Single icon mode (draw centered)
    GPoint center = GPoint(bounds.size.w/2, bounds.size.h/2);
    if (s_sky_code == 0) {
      // Sun
      graphics_fill_circle(ctx, center, 5);
      for (int r = 0; r < 8; r++) {
        int angle = r * 45;
        int16_t sx = center.x + (int16_t)(sin_lookup(TRIG_MAX_ANGLE * angle / 360) * 7 / TRIG_MAX_RATIO);
        int16_t sy = center.y - (int16_t)(cos_lookup(TRIG_MAX_ANGLE * angle / 360) * 7 / TRIG_MAX_RATIO);
        graphics_fill_circle(ctx, GPoint(sx, sy), 1);
      }
    } else if (s_sky_code == 1) {
      // Cloud (raised)
      const int CLOUD_Y_OFFSET_SINGLE = -1;
      GPoint p1 = GPoint(center.x - 3, center.y + 1 + CLOUD_Y_OFFSET_SINGLE);
      GPoint p2 = GPoint(center.x + 3, center.y + 1 + CLOUD_Y_OFFSET_SINGLE);
      graphics_fill_circle(ctx, p1, 4);
      graphics_fill_circle(ctx, p2, 4);
      graphics_fill_rect(ctx, GRect(center.x - 8, center.y + 1 + CLOUD_Y_OFFSET_SINGLE, 16, 6), 2, GCornersAll);
    } else {
      // Rain: teardrop oriented with round bottom and triangular top (single icon)
      int dx = center.x;
      int dy_bot = center.y + 3; // lift the drop by 2 pixels
      int bottom_radius = 4.5;
      graphics_fill_circle(ctx, GPoint(dx, dy_bot), bottom_radius);
      {
        int base_y = dy_bot - bottom_radius - 1;
  GPathInfo tri_info = {3, (GPoint[]){{0, -5}, {-3, 0}, {3, 0}}};
        GPath *tri = gpath_create(&tri_info);
        gpath_move_to(tri, GPoint(dx, base_y));
        gpath_draw_filled(ctx, tri);
        gpath_destroy(tri);
      }
    }
  }
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

  // Place date just above the time
  s_date_layer = text_layer_create(GRect(0, 18, bounds.size.w, 20));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  // Move main time down to make room; keep large numeric font
  s_time_layer = text_layer_create(GRect(0, 36, bounds.size.w, 40));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  // Create a single small sky icon layer positioned in the upper-left corner
  const int ICON_SIZE = 16;
  int sky_x = 0;
  int sky_y = 0;
  s_sky_layer = layer_create(GRect(sky_x, sky_y, ICON_SIZE, ICON_SIZE));
  layer_set_update_proc(s_sky_layer, sky_layer_update_proc);
  layer_add_child(window_layer, s_sky_layer);

  // Place weather (temp & humidity) to the right of the sky icon (no labels)
  const int sky_icon_right = sky_x + ICON_SIZE; // sky_x + ICON_SIZE from above
  int top_metric_y = -4; // same vertical alignment for weather metrics
  // Temperature (bold 18) and humidity (regular) placed to the right of sky icon
  int temp_x = sky_icon_right + 2;
  s_temp_layer = text_layer_create(GRect(temp_x, top_metric_y, 50, 20));
  text_layer_set_background_color(s_temp_layer, GColorClear);
  text_layer_set_text_color(s_temp_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_temp_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_temp_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_temp_layer));

  s_hum_layer = text_layer_create(GRect(temp_x + 52, top_metric_y, 60, 20));
  text_layer_set_background_color(s_hum_layer, GColorClear);
  text_layer_set_text_color(s_hum_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_hum_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_hum_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_hum_layer));

  // Place min/max in the upper-right corner, same vertical alignment and font
  s_minmax_layer = text_layer_create(GRect(bounds.size.w - 90, top_metric_y, 86, 20));
  text_layer_set_background_color(s_minmax_layer, GColorClear);
  text_layer_set_text_color(s_minmax_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_minmax_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_minmax_layer, GTextAlignmentRight);
  layer_add_child(window_layer, text_layer_get_layer(s_minmax_layer));

  // Position sunrise/sunset at the very bottom, status line just above it
  /* Adjusted for smaller suntime font (14px): make the sun lines shorter and
    reduce bottom margin so they sit closer to the screen bottom. */
  const int SUN_HEIGHT = 14;
  const int STATUS_HEIGHT = 18;
  const int BOTTOM_MARGIN = 2;
  // Sunrise left, sunset right at the very bottom
  GRect sunrise_frame = GRect(4, bounds.size.h - SUN_HEIGHT - BOTTOM_MARGIN, bounds.size.w/2 - 4, SUN_HEIGHT);
  GRect sunset_frame = GRect(bounds.size.w/2, bounds.size.h - SUN_HEIGHT - BOTTOM_MARGIN, bounds.size.w/2 - 4, SUN_HEIGHT);
  s_sunrise_layer = text_layer_create(sunrise_frame);
  text_layer_set_background_color(s_sunrise_layer, GColorClear);
  text_layer_set_text_color(s_sunrise_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_sunrise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_sunrise_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_sunrise_layer));

  s_sunset_layer = text_layer_create(sunset_frame);
  text_layer_set_background_color(s_sunset_layer, GColorClear);
  text_layer_set_text_color(s_sunset_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_sunset_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_sunset_layer, GTextAlignmentRight);
  layer_add_child(window_layer, text_layer_get_layer(s_sunset_layer));

  // Status line just above sunrise/sunset
  GRect status_frame = GRect(0, bounds.size.h - SUN_HEIGHT - STATUS_HEIGHT - BOTTOM_MARGIN - 2, bounds.size.w, STATUS_HEIGHT);
  s_status_layer = text_layer_create(status_frame);
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_text(s_status_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(s_status_layer));
  // Ensure status line is visible in normal operation
  layer_set_hidden(text_layer_get_layer(s_status_layer), false);


  prv_update_time();
  prv_format_and_update_weather();
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_temp_layer);
  text_layer_destroy(s_hum_layer);
  text_layer_destroy(s_minmax_layer);
  layer_destroy(s_sky_layer);
  text_layer_destroy(s_sunrise_layer);
  text_layer_destroy(s_sunset_layer);
  text_layer_destroy(s_status_layer);
}

static void prv_init(void) {
  // Apply persisted dark mode (if set), otherwise use default
  if (persist_exists(PERSIST_KEY_DARK_MODE)) {
    int saved = persist_read_int(PERSIST_KEY_DARK_MODE);
    prv_set_dark_mode(saved ? true : false);
  } else {
    prv_set_dark_mode(s_dark_mode);
  }

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
  if (s_temp_layer) text_layer_set_text_color(s_temp_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_hum_layer) text_layer_set_text_color(s_hum_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_minmax_layer) text_layer_set_text_color(s_minmax_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_sunrise_layer) text_layer_set_text_color(s_sunrise_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_sunset_layer) text_layer_set_text_color(s_sunset_layer, s_dark_mode ? GColorWhite : GColorBlack);
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
