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

// Dark mode flag (user option to toggle later). true = black background, white text.
static bool s_dark_mode = false;

// Time font selection - change this to switch between different font bitmaps
static const char* time_font = "leco-84-regular"; // Options: "leco", "leco-bold", "entsans" (add more fonts as needed)
// Date font selection - change this to switch between different date font bitmaps
static const char* date_font = "leco-42-regular"; // Options: "leco-42-regular" (add more date fonts as needed)
static GFont s_icon_font = NULL;
static GFont s_sky_font = NULL; // FONT_WEATHER_12 for the small sky glyph


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

// Bitmap digit layers for large time display
static BitmapLayer *s_hour_tens_layer, *s_hour_ones_layer;
static BitmapLayer *s_minute_tens_layer, *s_minute_ones_layer;
static GBitmap *s_hour_tens_bitmap, *s_hour_ones_bitmap;
static GBitmap *s_minute_tens_bitmap, *s_minute_ones_bitmap;

// Bitmap digit layers for date complication (half-size)
static BitmapLayer *s_month_tens_layer, *s_month_ones_layer;
static BitmapLayer *s_day_tens_layer, *s_day_ones_layer;
static GBitmap *s_month_tens_bitmap, *s_month_ones_bitmap;
static GBitmap *s_day_tens_bitmap, *s_day_ones_bitmap;

static TextLayer *s_icon_test_layer;
static TextLayer *s_icon_glyph_layer; // shows the WeatherIcons glyph next to the icon code
static TextLayer *s_temperature_layer, *s_humidity_layer, *s_minmax_layer, *s_sunrise_layer, *s_sunset_layer, *s_status_layer;
static TextLayer *s_sky_glyph_layer;
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
// Note: No text buffers needed for time or date - both use bitmap digits now

// Debug helper: hide the day layer entirely while tuning month alignment
static bool s_debug_hide_day = true; // set true to hide day layer during debugging

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

// Helper function to get bitmap resource ID for a digit based on dark mode and font
static uint32_t get_digit_resource_id(int digit, const char* font_string, bool dark_mode) {
  // Use font-specific resource names based on time_font setting
  if (strcmp(font_string, "leco-84-regular") == 0) {
    if (dark_mode) {
      // White LECO digits on dark background to show bitmap edges
      switch (digit) {
        case 0: return RESOURCE_ID_DIGIT_0_LECO_84_REGULAR_WHITE;
        case 1: return RESOURCE_ID_DIGIT_1_LECO_84_REGULAR_WHITE;
        case 2: return RESOURCE_ID_DIGIT_2_LECO_84_REGULAR_WHITE;
        case 3: return RESOURCE_ID_DIGIT_3_LECO_84_REGULAR_WHITE;
        case 4: return RESOURCE_ID_DIGIT_4_LECO_84_REGULAR_WHITE;
        case 5: return RESOURCE_ID_DIGIT_5_LECO_84_REGULAR_WHITE;
        case 6: return RESOURCE_ID_DIGIT_6_LECO_84_REGULAR_WHITE;
        case 7: return RESOURCE_ID_DIGIT_7_LECO_84_REGULAR_WHITE;
        case 8: return RESOURCE_ID_DIGIT_8_LECO_84_REGULAR_WHITE;
        case 9: return RESOURCE_ID_DIGIT_9_LECO_84_REGULAR_WHITE;
        default: return RESOURCE_ID_DIGIT_0_LECO_84_REGULAR_WHITE; // fallback
      }
    } else {
      // Black LECO digits on light background
      switch (digit) {
        case 0: return RESOURCE_ID_DIGIT_0_LECO_84_REGULAR_BLACK;
        case 1: return RESOURCE_ID_DIGIT_1_LECO_84_REGULAR_BLACK;
        case 2: return RESOURCE_ID_DIGIT_2_LECO_84_REGULAR_BLACK;
        case 3: return RESOURCE_ID_DIGIT_3_LECO_84_REGULAR_BLACK;
        case 4: return RESOURCE_ID_DIGIT_4_LECO_84_REGULAR_BLACK;
        case 5: return RESOURCE_ID_DIGIT_5_LECO_84_REGULAR_BLACK;
        case 6: return RESOURCE_ID_DIGIT_6_LECO_84_REGULAR_BLACK;
        case 7: return RESOURCE_ID_DIGIT_7_LECO_84_REGULAR_BLACK;
        case 8: return RESOURCE_ID_DIGIT_8_LECO_84_REGULAR_BLACK;
        case 9: return RESOURCE_ID_DIGIT_9_LECO_84_REGULAR_BLACK;
        default: return RESOURCE_ID_DIGIT_0_LECO_84_REGULAR_BLACK; // fallback
      }
    }
  }

    if (strcmp(font_string, "leco-42-regular") == 0) {
    if (dark_mode) {
      // White LECO digits on dark background to show bitmap edges
      switch (digit) {
        case 0: return RESOURCE_ID_DIGIT_0_LECO_42_REGULAR_WHITE;
        case 1: return RESOURCE_ID_DIGIT_1_LECO_42_REGULAR_WHITE;
        case 2: return RESOURCE_ID_DIGIT_2_LECO_42_REGULAR_WHITE;
        case 3: return RESOURCE_ID_DIGIT_3_LECO_42_REGULAR_WHITE;
        case 4: return RESOURCE_ID_DIGIT_4_LECO_42_REGULAR_WHITE;
        case 5: return RESOURCE_ID_DIGIT_5_LECO_42_REGULAR_WHITE;
        case 6: return RESOURCE_ID_DIGIT_6_LECO_42_REGULAR_WHITE;
        case 7: return RESOURCE_ID_DIGIT_7_LECO_42_REGULAR_WHITE;
        case 8: return RESOURCE_ID_DIGIT_8_LECO_42_REGULAR_WHITE;
        case 9: return RESOURCE_ID_DIGIT_9_LECO_42_REGULAR_WHITE;
        default: return RESOURCE_ID_DIGIT_0_LECO_42_REGULAR_WHITE; // fallback
      }
    } else {
      // Black LECO digits on light background
      switch (digit) {
        case 0: return RESOURCE_ID_DIGIT_0_LECO_42_REGULAR_BLACK;
        case 1: return RESOURCE_ID_DIGIT_1_LECO_42_REGULAR_BLACK;
        case 2: return RESOURCE_ID_DIGIT_2_LECO_42_REGULAR_BLACK;
        case 3: return RESOURCE_ID_DIGIT_3_LECO_42_REGULAR_BLACK;
        case 4: return RESOURCE_ID_DIGIT_4_LECO_42_REGULAR_BLACK;
        case 5: return RESOURCE_ID_DIGIT_5_LECO_42_REGULAR_BLACK;
        case 6: return RESOURCE_ID_DIGIT_6_LECO_42_REGULAR_BLACK;
        case 7: return RESOURCE_ID_DIGIT_7_LECO_42_REGULAR_BLACK;
        case 8: return RESOURCE_ID_DIGIT_8_LECO_42_REGULAR_BLACK;
        case 9: return RESOURCE_ID_DIGIT_9_LECO_42_REGULAR_BLACK;
        default: return RESOURCE_ID_DIGIT_0_LECO_42_REGULAR_BLACK; // fallback
      }
    }
  }

  if (strcmp(font_string, "leco-bold") == 0) {
    if (dark_mode) {
      // White LECO digits on dark background to show bitmap edges
      switch (digit) {
        case 0: return RESOURCE_ID_DIGIT_0_LECO_84_BOLD_WHITE;
        case 1: return RESOURCE_ID_DIGIT_1_LECO_84_BOLD_WHITE;
        case 2: return RESOURCE_ID_DIGIT_2_LECO_84_BOLD_WHITE;
        case 3: return RESOURCE_ID_DIGIT_3_LECO_84_BOLD_WHITE;
        case 4: return RESOURCE_ID_DIGIT_4_LECO_84_BOLD_WHITE;
        case 5: return RESOURCE_ID_DIGIT_5_LECO_84_BOLD_WHITE;
        case 6: return RESOURCE_ID_DIGIT_6_LECO_84_BOLD_WHITE;
        case 7: return RESOURCE_ID_DIGIT_7_LECO_84_BOLD_WHITE;
        case 8: return RESOURCE_ID_DIGIT_8_LECO_84_BOLD_WHITE;
        case 9: return RESOURCE_ID_DIGIT_9_LECO_84_BOLD_WHITE;
        default: return RESOURCE_ID_DIGIT_0_LECO_84_BOLD_WHITE; // fallback
      }
    } else {
      // Black LECO_84_BOLD digits on light background
      switch (digit) {
        case 0: return RESOURCE_ID_DIGIT_0_LECO_84_BOLD_BLACK;
        case 1: return RESOURCE_ID_DIGIT_1_LECO_84_BOLD_BLACK;
        case 2: return RESOURCE_ID_DIGIT_2_LECO_84_BOLD_BLACK;
        case 3: return RESOURCE_ID_DIGIT_3_LECO_84_BOLD_BLACK;
        case 4: return RESOURCE_ID_DIGIT_4_LECO_84_BOLD_BLACK;
        case 5: return RESOURCE_ID_DIGIT_5_LECO_84_BOLD_BLACK;
        case 6: return RESOURCE_ID_DIGIT_6_LECO_84_BOLD_BLACK;
        case 7: return RESOURCE_ID_DIGIT_7_LECO_84_BOLD_BLACK;
        case 8: return RESOURCE_ID_DIGIT_8_LECO_84_BOLD_BLACK;
        case 9: return RESOURCE_ID_DIGIT_9_LECO_84_BOLD_BLACK;
        default: return RESOURCE_ID_DIGIT_0_LECO_84_BOLD_BLACK; // fallback
      }
    }
  }

  if (strcmp(font_string, "entsans") == 0) {
    if (dark_mode) {
      // White ENTSANS_70 digits on dark background to show bitmap edges
      switch (digit) {
        case 0: return RESOURCE_ID_DIGIT_0_ENTSANS_70_WHITE;
        case 1: return RESOURCE_ID_DIGIT_1_ENTSANS_70_WHITE;
        case 2: return RESOURCE_ID_DIGIT_2_ENTSANS_70_WHITE;
        case 3: return RESOURCE_ID_DIGIT_3_ENTSANS_70_WHITE;
        case 4: return RESOURCE_ID_DIGIT_4_ENTSANS_70_WHITE;
        case 5: return RESOURCE_ID_DIGIT_5_ENTSANS_70_WHITE;
        case 6: return RESOURCE_ID_DIGIT_6_ENTSANS_70_WHITE;
        case 7: return RESOURCE_ID_DIGIT_7_ENTSANS_70_WHITE;
        case 8: return RESOURCE_ID_DIGIT_8_ENTSANS_70_WHITE;
        case 9: return RESOURCE_ID_DIGIT_9_ENTSANS_70_WHITE;
        default: return RESOURCE_ID_DIGIT_0_ENTSANS_70_WHITE; // fallback
      }
    } else {
      // Black ENTSANS_70 digits on light background
      switch (digit) {
        case 0: return RESOURCE_ID_DIGIT_0_ENTSANS_70_BLACK;
        case 1: return RESOURCE_ID_DIGIT_1_ENTSANS_70_BLACK;
        case 2: return RESOURCE_ID_DIGIT_2_ENTSANS_70_BLACK;
        case 3: return RESOURCE_ID_DIGIT_3_ENTSANS_70_BLACK;
        case 4: return RESOURCE_ID_DIGIT_4_ENTSANS_70_BLACK;
        case 5: return RESOURCE_ID_DIGIT_5_ENTSANS_70_BLACK;
        case 6: return RESOURCE_ID_DIGIT_6_ENTSANS_70_BLACK;
        case 7: return RESOURCE_ID_DIGIT_7_ENTSANS_70_BLACK;
        case 8: return RESOURCE_ID_DIGIT_8_ENTSANS_70_BLACK;
        case 9: return RESOURCE_ID_DIGIT_9_ENTSANS_70_BLACK;
        default: return RESOURCE_ID_DIGIT_0_ENTSANS_70_BLACK; // fallback
      }
    }
  }

  // Fallback to LECO digits if font not recognized (default case)
  if (dark_mode) {
    switch (digit) {
      case 0: return RESOURCE_ID_DIGIT_0_LECO_WHITE;
      case 1: return RESOURCE_ID_DIGIT_1_LECO_WHITE;
      case 2: return RESOURCE_ID_DIGIT_2_LECO_WHITE;
      case 3: return RESOURCE_ID_DIGIT_3_LECO_WHITE;
      case 4: return RESOURCE_ID_DIGIT_4_LECO_WHITE;
      case 5: return RESOURCE_ID_DIGIT_5_LECO_WHITE;
      case 6: return RESOURCE_ID_DIGIT_6_LECO_WHITE;
      case 7: return RESOURCE_ID_DIGIT_7_LECO_WHITE;
      case 8: return RESOURCE_ID_DIGIT_8_LECO_WHITE;
      case 9: return RESOURCE_ID_DIGIT_9_LECO_WHITE;
      default: return RESOURCE_ID_DIGIT_0_LECO_WHITE;
    }
  } else {
    switch (digit) {
      case 0: return RESOURCE_ID_DIGIT_0_LECO_BLACK;
      case 1: return RESOURCE_ID_DIGIT_1_LECO_BLACK;
      case 2: return RESOURCE_ID_DIGIT_2_LECO_BLACK;
      case 3: return RESOURCE_ID_DIGIT_3_LECO_BLACK;
      case 4: return RESOURCE_ID_DIGIT_4_LECO_BLACK;
      case 5: return RESOURCE_ID_DIGIT_5_LECO_BLACK;
      case 6: return RESOURCE_ID_DIGIT_6_LECO_BLACK;
      case 7: return RESOURCE_ID_DIGIT_7_LECO_BLACK;
      case 8: return RESOURCE_ID_DIGIT_8_LECO_BLACK;
      case 9: return RESOURCE_ID_DIGIT_9_LECO_BLACK;
      default: return RESOURCE_ID_DIGIT_0_LECO_BLACK;
    }
  }
}

static void prv_update_time() {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  int hour, minute;
  
  // Get hour and minute as integers
  if (clock_is_24h_style()) {
    hour = tick_time->tm_hour;
    minute = tick_time->tm_min;
  } else {
    hour = tick_time->tm_hour;
    if (hour == 0) hour = 12;
    else if (hour > 12) hour -= 12;
    minute = tick_time->tm_min;
  }
  
  // Update bitmap digits for hour (always 2 digits in 24h, may be 1-2 digits in 12h)
  int hour_tens = hour / 10;
  int hour_ones = hour % 10;
  int minute_tens = minute / 10;
  int minute_ones = minute % 10;
  
  // Update hour digit bitmaps
  if (s_hour_tens_bitmap) gbitmap_destroy(s_hour_tens_bitmap);
  if (s_hour_ones_bitmap) gbitmap_destroy(s_hour_ones_bitmap);
  
  // For 12-hour format, hide tens digit if hour < 10
  if (!clock_is_24h_style() && hour < 10) {
    s_hour_tens_bitmap = NULL;
    bitmap_layer_set_bitmap(s_hour_tens_layer, NULL);
  } else {
    s_hour_tens_bitmap = gbitmap_create_with_resource(get_digit_resource_id(hour_tens, time_font, s_dark_mode));
    bitmap_layer_set_bitmap(s_hour_tens_layer, s_hour_tens_bitmap);
  }
  bitmap_layer_set_background_color(s_hour_tens_layer, GColorClear);
  
  s_hour_ones_bitmap = gbitmap_create_with_resource(get_digit_resource_id(hour_ones, time_font, s_dark_mode));
  bitmap_layer_set_bitmap(s_hour_ones_layer, s_hour_ones_bitmap);
  bitmap_layer_set_background_color(s_hour_ones_layer, GColorClear);
  
  // Update minute digit bitmaps (always 2 digits)
  if (s_minute_tens_bitmap) gbitmap_destroy(s_minute_tens_bitmap);
  if (s_minute_ones_bitmap) gbitmap_destroy(s_minute_ones_bitmap);
  
  s_minute_tens_bitmap = gbitmap_create_with_resource(get_digit_resource_id(minute_tens, time_font, s_dark_mode));
  s_minute_ones_bitmap = gbitmap_create_with_resource(get_digit_resource_id(minute_ones, time_font, s_dark_mode));
  
  bitmap_layer_set_bitmap(s_minute_tens_layer, s_minute_tens_bitmap);
  bitmap_layer_set_bitmap(s_minute_ones_layer, s_minute_ones_bitmap);
  bitmap_layer_set_background_color(s_minute_tens_layer, GColorClear);
  bitmap_layer_set_background_color(s_minute_ones_layer, GColorClear);

  // Font size testing: measure how wide 3 digits would be with different LECO font sizes
  if (s_window) {
    GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
    int screen_width = bounds.size.w; // Should be 144 for Pebble Time
    
    // Test string of 3 digits (worst case width scenario)
    const char* test_string = "888";
    
    // Test available LECO font sizes and log the results
    #ifdef RESOURCE_ID_FONT_LECO_47
      GFont leco_47 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LECO_47));
      if (leco_47) {
        GSize size_47 = graphics_text_layout_get_content_size(test_string, leco_47, 
          GRect(0, 0, screen_width, 100), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
        APP_LOG(APP_LOG_LEVEL_INFO, "LECO_47: '888' width=%d, screen=%d", size_47.w, screen_width);
        fonts_unload_custom_font(leco_47);
      }
    #endif
    
    #ifdef RESOURCE_ID_FONT_LECO_21
      GFont leco_21 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LECO_21));
      if (leco_21) {
        GSize size_21 = graphics_text_layout_get_content_size(test_string, leco_21, 
          GRect(0, 0, screen_width, 100), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
        APP_LOG(APP_LOG_LEVEL_INFO, "LECO_21: '888' width=%d, screen=%d", size_21.w, screen_width);
        fonts_unload_custom_font(leco_21);
      }
    #endif
  }

  // Month and day for complication (bitmap digits)
  int month = tick_time->tm_mon + 1; // tm_mon is 0-11, we want 1-12
  int day = tick_time->tm_mday;
  
  // Update month digit bitmaps
  int month_tens = month / 10;
  int month_ones = month % 10;
  int day_tens = day / 10;
  int day_ones = day % 10;
  
  // Destroy old bitmaps
  if (s_month_tens_bitmap) gbitmap_destroy(s_month_tens_bitmap);
  if (s_month_ones_bitmap) gbitmap_destroy(s_month_ones_bitmap);
  if (s_day_tens_bitmap) gbitmap_destroy(s_day_tens_bitmap);
  if (s_day_ones_bitmap) gbitmap_destroy(s_day_ones_bitmap);
  
  // Create new month bitmaps (use date_font for date)
  if (month_tens > 0) {
    s_month_tens_bitmap = gbitmap_create_with_resource(get_digit_resource_id(month_tens, date_font, s_dark_mode));
    bitmap_layer_set_bitmap(s_month_tens_layer, s_month_tens_bitmap);
  } else {
    s_month_tens_bitmap = NULL;
    bitmap_layer_set_bitmap(s_month_tens_layer, NULL); // Hide tens for months 1-9
  }
  bitmap_layer_set_background_color(s_month_tens_layer, GColorClear);
  
  s_month_ones_bitmap = gbitmap_create_with_resource(get_digit_resource_id(month_ones, date_font, s_dark_mode));
  bitmap_layer_set_bitmap(s_month_ones_layer, s_month_ones_bitmap);
  bitmap_layer_set_background_color(s_month_ones_layer, GColorClear);
  
  // Create new day bitmaps
  if (day_tens > 0) {
    s_day_tens_bitmap = gbitmap_create_with_resource(get_digit_resource_id(day_tens, date_font, s_dark_mode));
    bitmap_layer_set_bitmap(s_day_tens_layer, s_day_tens_bitmap);
  } else {
    s_day_tens_bitmap = NULL;
    bitmap_layer_set_bitmap(s_day_tens_layer, NULL); // Hide tens for days 1-9
  }
  bitmap_layer_set_background_color(s_day_tens_layer, GColorClear);
  
  s_day_ones_bitmap = gbitmap_create_with_resource(get_digit_resource_id(day_ones, date_font, s_dark_mode));
  bitmap_layer_set_bitmap(s_day_ones_layer, s_day_ones_bitmap);
  bitmap_layer_set_background_color(s_day_ones_layer, GColorClear);
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
    // Per design: do NOT synthesize or show a fallback glyph here. Leave
    // the glyph buffer empty so the UI can hide glyphs when none provided.
    s_sky_glyph_buf[0] = '\0';
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

    // Show the raw OWM icon code in the icon test layer (Roboto) for debugging.
    // Only show the glyph layer if the companion provided an explicit glyph.
    if (s_icon_code_buf[0]) {
      text_layer_set_text(s_icon_test_layer, s_icon_code_buf);
      text_layer_set_text_color(s_icon_test_layer, s_dark_mode ? GColorWhite : GColorBlack);
      layer_set_hidden(text_layer_get_layer(s_icon_test_layer), false);
      if (s_sky_glyph_buf[0]) {
        text_layer_set_text(s_icon_glyph_layer, s_sky_glyph_buf);
        text_layer_set_text_color(s_icon_glyph_layer, s_dark_mode ? GColorWhite : GColorBlack);
        layer_set_hidden(text_layer_get_layer(s_icon_glyph_layer), false);
      } else {
        layer_set_hidden(text_layer_get_layer(s_icon_glyph_layer), true);
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

  // Large bitmap digit layout: 2/3 screen width per time block
  const int DIGIT_W = 48;          // Width for each digit bitmap (96/2 = 48 per digit)
  const int DIGIT_H = 68;          // Height for each digit (matches regenerated bitmap size)
  const int BLOCK_GAP = 1;         // Vertical gap between hour and minute (minimal gap)
  const int BLOCK_W = 96;          // Total width for 2-digit block (2/3 of 144px screen)
  
  // Center the time blocks vertically
  int total_time_h = (DIGIT_H * 2) + BLOCK_GAP;
  int time_start_y = (bounds.size.h - total_time_h) / 2;
  
  // Hour block: left side of screen (0 to 96)
  int hour_x = 0;
  int hour_y = time_start_y;
  
  // Hour tens digit
  s_hour_tens_layer = bitmap_layer_create(GRect(hour_x, hour_y, DIGIT_W, DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_hour_tens_layer));
  
  // Hour ones digit 
  s_hour_ones_layer = bitmap_layer_create(GRect(hour_x + DIGIT_W, hour_y, DIGIT_W, DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_hour_ones_layer));
  
  // Minute block: positioned below hour block and right-aligned with screen
  int minute_x = bounds.size.w - BLOCK_W;  // Right-aligned with screen edge
  int minute_y = time_start_y + DIGIT_H + BLOCK_GAP;
  
  // Minute tens digit
  s_minute_tens_layer = bitmap_layer_create(GRect(minute_x, minute_y, DIGIT_W, DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_minute_tens_layer));
  
  // Minute ones digit
  s_minute_ones_layer = bitmap_layer_create(GRect(minute_x + DIGIT_W, minute_y, DIGIT_W, DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_minute_ones_layer));
  
  // Initialize bitmap pointers to NULL
  s_hour_tens_bitmap = NULL;
  s_hour_ones_bitmap = NULL;
  s_minute_tens_bitmap = NULL;  
  s_minute_ones_bitmap = NULL;
  

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
  int icon_test_y = minute_y + DIGIT_H + 8; // Position below the minute block
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
  // End Icon test layer



  // Month and day complication to the right of the time blocks (bitmap digits)
  // Time blocks: 0-96, Available right space: 96-144 = 48 pixels
  const int DATE_DIGIT_W = 24;     // Half-size digit width (48/2)
  const int DATE_DIGIT_H = 34;     // Half-size digit height (68/2)
  int date_x = BLOCK_W;            // No gap after time blocks
  
  // Calculate month and day heights and positions for alignment
  int half_h = DIGIT_H / 2;  // Exactly half the hour block height (68/2 = 34)
  
  // Month digits: top half aligned with hour top
  int month_y = hour_y;
  
  // Month tens digit (hidden for months 1-9)
  s_month_tens_layer = bitmap_layer_create(GRect(date_x, month_y, DATE_DIGIT_W, DATE_DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_month_tens_layer));
  
  // Month ones digit
  s_month_ones_layer = bitmap_layer_create(GRect(date_x + DATE_DIGIT_W, month_y, DATE_DIGIT_W, DATE_DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_month_ones_layer));
  
  // Day digits: bottom half aligned with hour bottom
  int day_y = hour_y + half_h;
  
  // Day tens digit (hidden for days 1-9)
  s_day_tens_layer = bitmap_layer_create(GRect(date_x, day_y, DATE_DIGIT_W, DATE_DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_day_tens_layer));
  
  // Day ones digit
  s_day_ones_layer = bitmap_layer_create(GRect(date_x + DATE_DIGIT_W, day_y, DATE_DIGIT_W, DATE_DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_day_ones_layer));
  
  // Initialize bitmap pointers to NULL
  s_month_tens_bitmap = NULL;
  s_month_ones_bitmap = NULL;
  s_day_tens_bitmap = NULL;
  s_day_ones_bitmap = NULL;

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


  prv_update_time();
  prv_format_and_update_weather();
}

static void prv_window_unload(Window *window) {
  // Destroy bitmap layers and resources for digit display
  if (s_hour_tens_bitmap) gbitmap_destroy(s_hour_tens_bitmap);
  if (s_hour_ones_bitmap) gbitmap_destroy(s_hour_ones_bitmap);
  if (s_minute_tens_bitmap) gbitmap_destroy(s_minute_tens_bitmap);
  if (s_minute_ones_bitmap) gbitmap_destroy(s_minute_ones_bitmap);
  
  // Destroy bitmap layers and resources for date display
  if (s_month_tens_bitmap) gbitmap_destroy(s_month_tens_bitmap);
  if (s_month_ones_bitmap) gbitmap_destroy(s_month_ones_bitmap);
  if (s_day_tens_bitmap) gbitmap_destroy(s_day_tens_bitmap);
  if (s_day_ones_bitmap) gbitmap_destroy(s_day_ones_bitmap);
  
  bitmap_layer_destroy(s_hour_tens_layer);
  bitmap_layer_destroy(s_hour_ones_layer);
  bitmap_layer_destroy(s_minute_tens_layer);
  bitmap_layer_destroy(s_minute_ones_layer);
  
  bitmap_layer_destroy(s_month_tens_layer);
  bitmap_layer_destroy(s_month_ones_layer);
  bitmap_layer_destroy(s_day_tens_layer);
  bitmap_layer_destroy(s_day_ones_layer);
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
  if (s_temperature_layer) text_layer_set_text_color(s_temperature_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_humidity_layer) text_layer_set_text_color(s_humidity_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_minmax_layer) text_layer_set_text_color(s_minmax_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_sunrise_layer) text_layer_set_text_color(s_sunrise_layer, s_dark_mode ? GColorWhite : GColorBlack);
  if (s_sunset_layer) text_layer_set_text_color(s_sunset_layer, s_dark_mode ? GColorWhite : GColorBlack);
    if (s_status_layer) text_layer_set_text_color(s_status_layer, s_dark_mode ? GColorWhite : GColorBlack);
    
    // Update time to refresh bitmap colors for both time and date digits
    prv_update_time();
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
