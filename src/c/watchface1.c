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
#include "weather.h"

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

// Fallback for CITY message key (numeric mapping). Companion uses 10011.
#ifndef MESSAGE_KEY_CITY
#define MESSAGE_KEY_CITY 10011
#endif

static void prv_format_and_update_weather(void);

static Window *s_window;
static TextLayer *s_time_layer, *s_date_layer;
static TextLayer *s_icon_test_layer;
static TextLayer *s_icon_glyph_layer; // shows the WeatherIcons glyph next to the icon code
static TextLayer *s_temperature_layer, *s_humidity_layer, *s_minmax_layer, *s_sunrise_layer, *s_sunset_layer, *s_status_layer;
static TextLayer *s_sky_glyph_layer;
static bool s_sky_test_all = false; // when true, draw all three icons for testing
static GFont s_date_font = NULL;
static GFont s_time_font = NULL;
static GFont s_icon_font = NULL;
static GFont s_sky_font = NULL; // FONT_WEATHER_12 for the small sky glyph

// State
static int s_temp = 0;
static int s_humidity = 0;
static int s_min = 0;
static int s_max = 0;
static time_t s_sunrise = 0;
static time_t s_sunset = 0;
static bool s_bt_connected = true;
static int s_battery_level = 100;
// Note: weather request cooldown is managed inside the weather module.
static bool s_prev_bt_connected = true;
// Per-layer persistent text buffers (TextLayer stores the pointer)
// static char s_weather_buf[64]; // no longer used; replaced by s_temperature_buffer and s_hum_buf
static char s_temperature_buffer[32];
static char s_hum_buf[32];
static char s_minmax_buf[64];
static char s_sunrise_buf[32];
static char s_sunset_buf[32];
static char s_status_buf[32];
static char s_city_buf[32];
static char s_sky_glyph_buf[8];
static char s_icon_code_buf[8];
// s_sky_code removed: glyphs provided by companion/module are used instead
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

/* Test trigger: Select button runs a sample weather payload */
static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  weather_run_sample_test();
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
}

/* Callback from weather module when new data arrives */
static void weather_module_cb(const weather_data_t *data, void *ctx) {
  if (!data) return;
  s_temp = data->temp;
  s_humidity = data->humidity;
  s_min = data->min;
  s_max = data->max;
  s_sunrise = data->sunrise;
  s_sunset = data->sunset;
  /* sky_code is no longer used; glyphs are provided by the weather module */
  strncpy(s_city_buf, data->city, sizeof(s_city_buf));
  s_city_buf[sizeof(s_city_buf)-1] = '\0';
  // Copy glyph (may be UTF-8 multi-byte); weather module uses null-terminated
  if (data->glyph && data->glyph[0]) {
    strncpy(s_sky_glyph_buf, data->glyph, sizeof(s_sky_glyph_buf));
    s_sky_glyph_buf[sizeof(s_sky_glyph_buf)-1] = '\0';
  } else {
    // If no glyph was provided, store a visible fallback glyph so the UI
    // doesn't show an empty box. Use U+F0B1 () from the weather icon set
    // or a placeholder character present in many fonts.
      strncpy(s_sky_glyph_buf, "", sizeof(s_sky_glyph_buf));
    s_sky_glyph_buf[sizeof(s_sky_glyph_buf)-1] = '\0';
  }
  // Copy raw OWM icon code string (like "01d") for display when present
  if (data->icon_code && data->icon_code[0]) {
    strncpy(s_icon_code_buf, data->icon_code, sizeof(s_icon_code_buf));
    s_icon_code_buf[sizeof(s_icon_code_buf)-1] = '\0';
  } else {
    s_icon_code_buf[0] = '\0';
  }
  prv_format_and_update_weather();
}

static void prv_format_and_update_weather() {
  // Weather line
  // Show compact temperature and humidity near the top-left icon (no labels)
  
  snprintf(s_hum_buf, sizeof(s_hum_buf), "%d%%", s_humidity);
  text_layer_set_text(s_humidity_layer, s_hum_buf);
  // Humidity is displayed centered at the top; no runtime reposition required.

  // Min/Max line
  // Show min/max compactly in upper-right as "min-max°"
  snprintf(s_minmax_buf, sizeof(s_minmax_buf), "%d-%d°", s_min, s_max);
  text_layer_set_text(s_minmax_layer, s_minmax_buf);

  snprintf(s_temperature_buffer, sizeof(s_temperature_buffer), "%d°C", s_temp);
  text_layer_set_text(s_temperature_layer, s_temperature_buffer);
  // Make the central sky+temp group responsive to text width: measure temp
  if (s_window && s_temperature_layer) {
  // Get screen bounds and glyph icon frame
  GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
  GRect sky_frame = layer_get_frame(text_layer_get_layer(s_sky_glyph_layer));
  int ICON_SIZE = sky_frame.size.w;
    const int GAP = 4;
    // Measure temp text width using the same font used by the layer
    GFont temp_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    GSize measured = graphics_text_layout_get_content_size(s_temperature_buffer, temp_font, GRect(0, 0, bounds.size.w, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    int temperature_width = measured.w;
    // Cap to available space to avoid overlapping edges
    int max_temp_w = bounds.size.w - ICON_SIZE - GAP - 8; // small margin
    if (temperature_width > max_temp_w) temperature_width = max_temp_w;
    // Compute centered group left position inside the gap between humidity and min/max
    // Measure actual frames for humidity and minmax so this works if their widths change
    GRect hum_frame = layer_get_frame(text_layer_get_layer(s_humidity_layer));
    GRect minmax_frame = layer_get_frame(text_layer_get_layer(s_minmax_layer));
    int hum_right = hum_frame.origin.x + hum_frame.size.w;
    int minmax_left = minmax_frame.origin.x;

    // Combined width: icon + gap + temperature width
    int combined_w = ICON_SIZE + GAP + temperature_width;
    // Preserve temperature Y (it may be intentionally offset) and keep sky at top (y=0)
    GRect temp_frame_before = layer_get_frame(text_layer_get_layer(s_temperature_layer));
    int temp_y = temp_frame_before.origin.y;
    const int SKY_Y = 0;
    // Cap combined width to available space (leave a small gutter)
    int available_w = minmax_left - hum_right;
    int gutter = 0;
    if (available_w <= gutter * 2) {
      // Not enough space, fallback to screen center using combined_w capped to screen
      if (combined_w > bounds.size.w - 16) combined_w = bounds.size.w - 16;
      int center_x = bounds.size.w / 2;
      int group_left = center_x - (combined_w / 2);
      // If sky is hidden, center only the temperature in the screen center
          // Cap temp width to screen width minus margins so it doesn't overflow
          int max_temp_w_screen = bounds.size.w - 16;
          if (max_temp_w_screen < 0) max_temp_w_screen = 0;
          if (temperature_width > max_temp_w_screen) temperature_width = max_temp_w_screen;
          int temp_x_center = center_x - (temperature_width / 2);
          layer_set_frame(text_layer_get_layer(s_temperature_layer), GRect(temp_x_center, temp_y, temperature_width, 20));
    } else {
      if (combined_w > available_w - gutter) combined_w = available_w - gutter;
      int group_left = hum_right + (available_w - combined_w) / 2;
      // If the sky layer is hidden, center only the temperature in the gap
        // Cap temp width to the available gap (leave room for a small gutter)
        int max_temp_w_gap = available_w - gutter * 2;
        if (max_temp_w_gap < 0) max_temp_w_gap = 0;
        if (temperature_width > max_temp_w_gap) temperature_width = max_temp_w_gap;
        int temp_x = hum_right + (available_w - temperature_width) / 2;
        layer_set_frame(text_layer_get_layer(s_temperature_layer), GRect(temp_x, temp_y, temperature_width, 20));
    }
    text_layer_set_overflow_mode(s_temperature_layer, GTextOverflowModeTrailingEllipsis);
  }

    // Always prefer the companion/module-provided glyph. If present, show it
    // and hide the procedural sky layer. If not present, show an empty glyph
    // layer (hidden) and leave the procedural drawing in place as a fallback.
    if (s_sky_glyph_buf[0]) {
      text_layer_set_text(s_sky_glyph_layer, s_sky_glyph_buf);
      text_layer_set_text_color(s_sky_glyph_layer, s_dark_mode ? GColorWhite : GColorBlack);
      layer_set_hidden(text_layer_get_layer(s_sky_glyph_layer), false);
    } else {
      // No glyph provided: keep glyph layer hidden and show procedural sky
      text_layer_set_text(s_sky_glyph_layer, "");
      // procedural layer removed; nothing else to hide/show
      layer_set_hidden(text_layer_get_layer(s_sky_glyph_layer), true);
    }

    // Show the raw OWM icon code in the icon test layer (Roboto) for debugging
    if (s_icon_code_buf[0]) {
      text_layer_set_text(s_icon_test_layer, s_icon_code_buf);
      text_layer_set_text_color(s_icon_test_layer, s_dark_mode ? GColorWhite : GColorBlack);
      layer_set_hidden(text_layer_get_layer(s_icon_test_layer), false);
      // Also show the glyph if we have one
      if (s_sky_glyph_buf[0]) {
        text_layer_set_text(s_icon_glyph_layer, s_sky_glyph_buf);
        text_layer_set_text_color(s_icon_glyph_layer, s_dark_mode ? GColorWhite : GColorBlack);
        layer_set_hidden(text_layer_get_layer(s_icon_glyph_layer), false);
      } else {
  text_layer_set_text(s_icon_glyph_layer, "");
  text_layer_set_text_color(s_icon_glyph_layer, s_dark_mode ? GColorWhite : GColorBlack);
  layer_set_hidden(text_layer_get_layer(s_icon_glyph_layer), false);
      }
    } else {
      layer_set_hidden(text_layer_get_layer(s_icon_test_layer), true);
      layer_set_hidden(text_layer_get_layer(s_icon_glyph_layer), true);
    }

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
  if (s_battery_level >= 0 && s_battery_level < 20) {
    snprintf(s_status_buf, sizeof(s_status_buf), "Battery: %d%%", s_battery_level);
    text_layer_set_text(s_status_layer, s_status_buf);
  } else if (!live_bt) {
    strncpy(s_status_buf, "BT Disconnect", sizeof(s_status_buf));
    s_status_buf[sizeof(s_status_buf)-1] = '\0';
    text_layer_set_text(s_status_layer, s_status_buf);
  } else {
    // No critical warnings; prefer to show city name if we have it.
    if (s_city_buf[0]) {
      text_layer_set_text(s_status_layer, s_city_buf);
    } else {
      text_layer_set_text(s_status_layer, "");
    }
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

  // Let weather module parse weather-related keys and notify via callback
  weather_handle_inbox(iter);

  // Non-weather keys handled here
  Tuple *t;
  t = dict_find(iter, MESSAGE_KEY_BT_CONNECTED);
  if (t) s_bt_connected = (bool)t->value->int32;
  t = dict_find(iter, MESSAGE_KEY_BATTERY_LEVEL);
  if (t) s_battery_level = (int)t->value->int32;

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
    // Delegate to weather module which will enforce its own cooldown.
    if (!weather_request()) {
      APP_LOG(APP_LOG_LEVEL_INFO, "weather_request() skipped due to cooldown inside module");
    }
  }
}

/* prv_request_weather removed: use weather_request() or weather_force_request() from the
   weather module which enforces cooldown internally. */

static void prv_battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  prv_format_and_update_weather();
}

// Draw a small filled icon for the sky condition in the top-left.
// Procedural sky icons removed. Glyphs (from companion/module) are used.

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time();
  // Every 30 minutes, request weather refresh
  if ((tick_time->tm_min % 20) == 0) {
    // Delegate to weather module which enforces cooldown
    if (!weather_request()) {
      APP_LOG(APP_LOG_LEVEL_INFO, "weather_request() skipped due to cooldown inside module (tick)");
    }
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

  // Positioning: center the main time on screen (vertically centered)
  const int TIME_H = 60;
  int time_y = (bounds.size.h - TIME_H) / 2;
  s_time_layer = text_layer_create(GRect(0, time_y, bounds.size.w, TIME_H));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, s_dark_mode ? GColorWhite : GColorBlack);
  // Try to load a custom time font (Prototype 48) if present
  #ifdef RESOURCE_ID_FONT_PROTOTYPE_48
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LECO_47));
  #endif
  if (s_time_font) {
    text_layer_set_font(s_time_layer, s_time_font);
  } else {
    text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  }
  // Center the time horizontally
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  // Icon test layer directly underneath the time so we can preview glyphs
  // from the weather icon font. Shows the first four glyphs from the
  // package.json characterRegex so you can see how they render.
  #ifdef RESOURCE_ID_FONT_WEATHER_24
    s_icon_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WEATHER_24));
  #endif
  #ifdef RESOURCE_ID_FONT_WEATHER_12
    s_sky_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WEATHER_12));
  #endif
  const int ICON_TEST_H = 28;
  int icon_test_y = time_y + TIME_H + 2;
  // Glyph layer (small square) sits left of the icon code text. Use 20px width
  // so it displays a single glyph clearly. It will be hidden unless a glyph
  // is provided by the companion/module.
  s_icon_glyph_layer = text_layer_create(GRect(6, icon_test_y + 2, 20, ICON_TEST_H - 4));
  text_layer_set_background_color(s_icon_glyph_layer, GColorClear);
  text_layer_set_text_color(s_icon_glyph_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_sky_font) text_layer_set_font(s_icon_glyph_layer, s_sky_font);
  else if (s_icon_font) text_layer_set_font(s_icon_glyph_layer, s_icon_font);
  else text_layer_set_font(s_icon_glyph_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_icon_glyph_layer, GTextAlignmentCenter);
  text_layer_set_text(s_icon_glyph_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(s_icon_glyph_layer));

  s_icon_test_layer = text_layer_create(GRect(32, icon_test_y, bounds.size.w - 32, ICON_TEST_H));
  text_layer_set_background_color(s_icon_test_layer, GColorClear);
  text_layer_set_text_color(s_icon_test_layer, s_dark_mode ? GColorWhite : GColorBlack);
  // Use a readable Roboto variant for the raw icon code display
  text_layer_set_font(s_icon_test_layer, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
  text_layer_set_text_alignment(s_icon_test_layer, GTextAlignmentLeft);
  // Initially empty; will be populated with the OWM icon code (e.g. "01d")
  text_layer_set_text(s_icon_test_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(s_icon_test_layer));

  // Date above time: left-justified and using LECO if available, otherwise fallback
  const int DATE_H = 28;
  int date_y = time_y - DATE_H - 4;
  // Make the date left-justified with full-width so text aligns to the left edge
  s_date_layer = text_layer_create(GRect(0, date_y, bounds.size.w, DATE_H));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, s_dark_mode ? GColorWhite : GColorBlack);
  // Try to load a custom LECO font if present in resources. If not present
  // the build won't define the RESOURCE_ID symbol and this block is skipped.
  #ifdef RESOURCE_ID_FONT_KONSTRUCT_335
    s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_KONSTRUCT_33));
  #endif
  if (s_date_font) {
    text_layer_set_font(s_date_layer, s_date_font);
  } else {
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  }
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  // Arrange top-row: center the sky icon and temperature as a group, put
  // humidity on the left, and min/max on the right.
  const int ICON_SIZE = 16;
  const int GAP = 4;
  int top_metric_y = -4; // small top margin

  // Combined group width: icon + gap + temp width
  const int TEMP_W = 60;
  int combined_w = ICON_SIZE + GAP + TEMP_W;
  int center_x = bounds.size.w / 2;
  int group_left = center_x - (combined_w / 2);

  // Sky icon (left of the temp within the centered group)
  int sky_x = group_left;
  int sky_y = 0;
  // Glyph layer (WeatherIcons font) placed where the old sky icon was.
  s_sky_glyph_layer = text_layer_create(GRect(sky_x, sky_y, ICON_SIZE, ICON_SIZE));
  text_layer_set_background_color(s_sky_glyph_layer, GColorClear);
  text_layer_set_text_color(s_sky_glyph_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_sky_font) text_layer_set_font(s_sky_glyph_layer, s_sky_font);
  else if (s_icon_font) text_layer_set_font(s_sky_glyph_layer, s_icon_font);
  else text_layer_set_font(s_sky_glyph_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_sky_glyph_layer, GTextAlignmentCenter);
  text_layer_set_text(s_sky_glyph_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(s_sky_glyph_layer));
  // Show glyph layer by default (procedural icons removed)
  layer_set_hidden(text_layer_get_layer(s_sky_glyph_layer), false);

  // Temperature immediately to the right of the icon, part of the centered group
  int temp_x = group_left + ICON_SIZE + GAP;
  s_temperature_layer = text_layer_create(GRect(temp_x, top_metric_y, TEMP_W, 20));
  text_layer_set_background_color(s_temperature_layer, GColorClear);
  text_layer_set_text_color(s_temperature_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_temperature_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_temperature_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_temperature_layer));

  // Humidity on the left edge (no x offset)
  int hum_w = 60;
  int hum_x = 0;
  s_humidity_layer = text_layer_create(GRect(hum_x, top_metric_y, hum_w, 20));
  text_layer_set_background_color(s_humidity_layer, GColorClear);
  text_layer_set_text_color(s_humidity_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_humidity_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_humidity_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_humidity_layer));

  // Min/max on the right edge
  s_minmax_layer = text_layer_create(GRect(bounds.size.w - 86, top_metric_y, 86, 20));
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

  // Status line: place it on the same baseline as the sunrise/sunset
  // and centered horizontally between them. Use the smaller sun font
  // height so it doesn't overlap the sunrise/sunset texts.
  const int STATUS_WIDTH = bounds.size.w / 2;
  const int STATUS_X = bounds.size.w / 4;
  GRect status_frame = GRect(STATUS_X, bounds.size.h - STATUS_HEIGHT - BOTTOM_MARGIN, STATUS_WIDTH, STATUS_HEIGHT);
  s_status_layer = text_layer_create(status_frame);
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_text(s_status_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(s_status_layer));
  // Ensure status line is visible in normal operation
  layer_set_hidden(text_layer_get_layer(s_status_layer), false);

  // Add a Select-button handler to run a local weather test (useful in emulator)
  window_set_click_config_provider(window, prv_click_config_provider);


  prv_update_time();
  prv_format_and_update_weather();
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_temperature_layer);
  text_layer_destroy(s_humidity_layer);
  text_layer_destroy(s_minmax_layer);
  text_layer_destroy(s_sky_glyph_layer);
  text_layer_destroy(s_icon_glyph_layer);
  text_layer_destroy(s_icon_test_layer);
  // Procedural sky layer removed; nothing to destroy here.
  text_layer_destroy(s_sunrise_layer);
  text_layer_destroy(s_sunset_layer);
  text_layer_destroy(s_status_layer);
  // Unload custom fonts if loaded
  #ifdef RESOURCE_ID_FONT_PROTOTYPE_48
    if (s_time_font) fonts_unload_custom_font(s_time_font);
  #endif
  #ifdef RESOURCE_ID_FONT_LECO_BOLD
    if (s_date_font) fonts_unload_custom_font(s_date_font);
  #endif
  #ifdef RESOURCE_ID_FONT_WEATHER_24
    if (s_icon_font) fonts_unload_custom_font(s_icon_font);
  #endif
  #ifdef RESOURCE_ID_FONT_WEATHER_12
    if (s_sky_font) fonts_unload_custom_font(s_sky_font);
  #endif
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

  // Initialize weather module and register callback
  weather_init(weather_module_cb, NULL);

  // Start periodic weather refresh (module will force an initial request).
  weather_start_periodic(20);
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
  if (s_temperature_layer) text_layer_set_text_color(s_temperature_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_humidity_layer) text_layer_set_text_color(s_humidity_layer, s_dark_mode ? GColorWhite : GColorBlack);
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
  weather_deinit();
  // Stop periodic polling (if enabled)
  weather_stop_periodic();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  APP_LOG(APP_LOG_LEVEL_INFO, "watchface1 initialized (diorite target)");
  app_event_loop();
  prv_deinit();
}
