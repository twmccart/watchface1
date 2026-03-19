/* Watchface with complications:
   - Time (hours top-left, minutes bottom-right) using BlockFace sprite sheets
   - Date (top-right, medium sprites)
   - Four complications (bottom-left, alongside the minute block), each 1/4 the
     height of the large digits (16px tall). Each complication occupies exactly
     4 MININUMBERS glyph slots (4×10px = 40px wide): slot 0 holds a small icon
     bitmap, slots 1–3 hold MININUMBERS sprite glyphs.
       Complication 0: weather  — weather icon + right-justified temperature
       Complication 1: blank    — empty row
       Complication 2: bluetooth — BT icon + blank glyphs
       Complication 3: battery  — battery icon + percentage digits
   - Sunrise/sunset times (bottom)
   - Communicates with PKJS companion via AppMessage
*/

#include <pebble.h>
#include <stdlib.h>
#include "message_keys.auto.h"
#include "weather.h"

static bool s_dark_mode = true;

static GFont s_icon_font = NULL;  // FONT_WEATHER_24 (kept for future use)
static GFont s_sky_font = NULL;   // FONT_WEATHER_12 (kept for future use)

// Large sprite sheet (IMG_BIGNUMBERS-fixed.png): 480x64
//   10 digits (0-9), each 48px wide x 64px tall, no padding — stride = 48px
#define SPRITE_LARGE_DIGIT_WIDTH     48
#define SPRITE_LARGE_DIGIT_HEIGHT    64
#define SPRITE_LARGE_ELEMENT_WIDTH   48
#define SPRITE_LARGE_ELEMENT_SPACING 48

// Medium sprite sheet (IMG_MIDINUMBERS-fixed.png): 240x30
//   11 elements: digits 0-9 then a blank (cols 216-239).
//   Each glyph is 18px wide; stride is 22px (18px content + 4px gap).
//   Element n starts at x = n * 22.
#define SPRITE_MEDIUM_DIGIT_WIDTH     20   // display/layer width
#define SPRITE_MEDIUM_DIGIT_HEIGHT    30
#define SPRITE_MEDIUM_ELEMENT_WIDTH   18   // actual glyph pixel width
#define SPRITE_MEDIUM_ELEMENT_SPACING 22   // stride between element starts

// Mini sprite sheet (IMG_MININUMBERS.png): 130x13
//   13 elements: digits 0-9, then hyphen/dash (index 10, for negative temps),
//   then an unknown glyph resembling a misshapen 'k' (index 11, purpose unknown),
//   then a trailing blank (index 12).
//   Stride is ~10px per element; each glyph is approximately 7-8px wide.
//   Element n starts at x = n * 10.
//
//   NOTE: 1-bit sub-bitmaps require byte-aligned x offsets (multiples of 8),
//   so gbitmap_create_as_sub_bitmap() cannot be used for arbitrary glyph indices.
//   Instead, each glyph slot uses a 10px-wide viewport Layer as a clip container,
//   with a full 130px BitmapLayer of the sprite sheet inside. Repositioning the
//   inner layer via layer_set_frame() selects which glyph is visible.
#define SPRITE_MINI_ELEMENT_SPACING 10
#define SPRITE_MINI_GLYPH_HEIGHT    13
#define SPRITE_MINI_SHEET_W         130

// Complication constants
#define COMP_COUNT       4
#define COMP_H           (SPRITE_LARGE_DIGIT_HEIGHT / 4)  // 16px
#define COMP_SLOT_W      SPRITE_MINI_ELEMENT_SPACING       // 10px per slot
#define COMP_BLANK_INDEX 12   // blank glyph in MININUMBERS sheet
#define COMP_DASH_INDEX  10   // hyphen/minus glyph in MININUMBERS sheet
#define COMP_K_INDEX     11   // mystery 'k' glyph — used as BT disconnected indicator

// Each complication: 1 icon BitmapLayer (slot 0) + 3 glyph slots (slots 1-3).
// Each glyph slot is a 10px clip Layer containing a 130px sprite BitmapLayer.
// Repositioning the sprite BitmapLayer within its clip parent selects the glyph.
typedef struct {
  BitmapLayer *icon_layer;
  GBitmap     *icon_bitmap;
  Layer       *glyph_clip[3];    // 10×13 viewport; clips child to one glyph slot
  BitmapLayer *glyph_sprite[3];  // full mini sprite sheet, repositioned per glyph
} Complication;

static GBitmap *s_time_sprite_bitmap = NULL;
static GBitmap *s_date_sprite_bitmap = NULL;
static GBitmap *s_mini_sprite        = NULL;

// Sub-bitmaps for digit display (created from sprite sheets)
static GBitmap *s_current_hour_tens_bitmap   = NULL;
static GBitmap *s_current_hour_ones_bitmap   = NULL;
static GBitmap *s_current_minute_tens_bitmap = NULL;
static GBitmap *s_current_minute_ones_bitmap = NULL;
static GBitmap *s_current_month_tens_bitmap  = NULL;
static GBitmap *s_current_month_ones_bitmap  = NULL;
static GBitmap *s_current_day_tens_bitmap    = NULL;
static GBitmap *s_current_day_ones_bitmap    = NULL;

// Fallback message key defines
#ifndef MESSAGE_KEY_DARK_MODE
#define MESSAGE_KEY_DARK_MODE 10009
#endif
#ifndef MESSAGE_KEY_SKY_COND
#define MESSAGE_KEY_SKY_COND 10006
#endif
#ifndef MESSAGE_KEY_CITY
#define MESSAGE_KEY_CITY 10011
#endif

static void prv_update_complications(void);
static void prv_set_dark_mode(bool enable);

static Window *s_window;

// Time digit layers
static BitmapLayer *s_hour_tens_layer, *s_hour_ones_layer;
static BitmapLayer *s_minute_tens_layer, *s_minute_ones_layer;

// Date digit layers
static BitmapLayer *s_month_tens_layer, *s_month_ones_layer;
static BitmapLayer *s_day_tens_layer,   *s_day_ones_layer;

// Top weather bar: humidity (left) + high/low (right)
static TextLayer *s_humidity_layer, *s_hilo_layer;

// Bottom row: sunrise/sunset times + status (city name)
static TextLayer *s_sunrise_layer, *s_sunset_layer, *s_status_layer;

// Four generic complications
static Complication s_comp[COMP_COUNT];

// Persistent storage keys
#define PERSIST_KEY_DARK_MODE  1
#define PERSIST_KEY_VIBRATE_BT 2
#define PERSIST_KEY_TEMP       3
#define PERSIST_KEY_SUNRISE    4
#define PERSIST_KEY_SUNSET     5
#define PERSIST_KEY_ICON_CODE  6
#define PERSIST_KEY_WEATHER_AT 7
#define PERSIST_KEY_HUMIDITY   8
#define PERSIST_KEY_MIN        9
#define PERSIST_KEY_MAX        10

// State
static int      s_temp          = 0;
static int      s_humidity      = 0;
static int      s_min           = 0;
static int      s_max           = 0;
static time_t   s_sunrise       = 0;
static time_t   s_sunset        = 0;
static bool     s_bt_connected  = true;
static int      s_battery_level = 100;
static bool     s_prev_bt_connected = true;
static bool     s_vibrate_bt    = true;  // vibrate on BT connect/disconnect

// Staleness thresholds
#define WEATHER_STALE_SECONDS (30 * 60)       // 30 min: temp/icon shown stale after this
#define SUNTIME_STALE_SECONDS (24 * 60 * 60)  // 1 day: sunrise/sunset kept until next day

static time_t s_weather_received_at = 0;  // 0 = never received

static AppTimer *s_suntime_hide_timer = NULL;

// String buffers
static char s_icon_code_buf[8];
static char s_city_buf[32];
static char s_sunrise_buf[32];
static char s_sunset_buf[32];
static char s_humidity_buf[8];
static char s_hilo_buf[16];

enum {
  KEY_WEATHER_TEMP     = 0,
  KEY_WEATHER_HUMIDITY = 1,
  KEY_WEATHER_MIN      = 2,
  KEY_WEATHER_MAX      = 3,
  KEY_SUNRISE          = 4,
  KEY_SUNSET           = 5,
  KEY_BT_CONNECTED     = 6,
  KEY_BATTERY_LEVEL    = 7,
  KEY_DATE_STRING      = 8,
  KEY_REQUEST_WEATHER  = 100
};

// Map OWM icon code (e.g. "01d") to the appropriate resource ID
static uint32_t prv_icon_code_to_resource(const char *icon_code) {
  if (!icon_code || !icon_code[0])              return RESOURCE_ID_ICON_CLOUD_ERROR;
  if (strcmp(icon_code, "01d") == 0)            return RESOURCE_ID_WEATHER_CLEAR_DAY;
  if (strcmp(icon_code, "01n") == 0)            return RESOURCE_ID_WEATHER_CLEAR_NIGHT;
  if (strcmp(icon_code, "02d") == 0)            return RESOURCE_ID_WEATHER_PARTLY_CLOUDY_DAY;
  if (strcmp(icon_code, "02n") == 0)            return RESOURCE_ID_WEATHER_PARTLY_CLOUDY_NIGHT;
  if (strcmp(icon_code, "03d") == 0 ||
      strcmp(icon_code, "03n") == 0 ||
      strcmp(icon_code, "04d") == 0 ||
      strcmp(icon_code, "04n") == 0)            return RESOURCE_ID_WEATHER_CLOUDY;
  if (strcmp(icon_code, "09d") == 0 ||
      strcmp(icon_code, "09n") == 0)            return RESOURCE_ID_WEATHER_DRIZZLE;
  if (strcmp(icon_code, "10d") == 0 ||
      strcmp(icon_code, "10n") == 0)            return RESOURCE_ID_WEATHER_RAIN;
  if (strcmp(icon_code, "11d") == 0 ||
      strcmp(icon_code, "11n") == 0)            return RESOURCE_ID_WEATHER_THUNDER;
  if (strcmp(icon_code, "13d") == 0 ||
      strcmp(icon_code, "13n") == 0)            return RESOURCE_ID_WEATHER_SNOW;
  if (strcmp(icon_code, "50d") == 0 ||
      strcmp(icon_code, "50n") == 0)            return RESOURCE_ID_WEATHER_FOG;
  return RESOURCE_ID_ICON_CLOUD_ERROR;
}

// Set a digit bitmap layer to display a specific digit from a sprite sheet
static void set_digit_from_sprite(BitmapLayer *layer, int digit, GBitmap *sprite_bitmap,
                                   int digit_width, int digit_height, GBitmap **cleanup_ref) {
  if (!layer || !sprite_bitmap || digit < 0 || digit > 9) return;

  if (cleanup_ref && *cleanup_ref) {
    gbitmap_destroy(*cleanup_ref);
    *cleanup_ref = NULL;
  }

  int element_width, element_spacing;
  if (digit_width == SPRITE_MEDIUM_DIGIT_WIDTH) {
    element_width   = SPRITE_MEDIUM_ELEMENT_WIDTH;
    element_spacing = SPRITE_MEDIUM_ELEMENT_SPACING;
  } else {
    element_width   = SPRITE_LARGE_ELEMENT_WIDTH;
    element_spacing = SPRITE_LARGE_ELEMENT_SPACING;
  }
  int x_offset = digit * element_spacing;

  GRect digit_bounds = GRect(x_offset, 0, element_width, digit_height);
  GBitmap *sub = gbitmap_create_as_sub_bitmap(sprite_bitmap, digit_bounds);
  if (sub) {
    bitmap_layer_set_bitmap(layer, sub);
    bitmap_layer_set_background_color(layer, GColorClear);
    if (cleanup_ref) *cleanup_ref = sub;
  }
}

// Set the icon bitmap for a complication slot
static void prv_comp_set_icon(int ci, uint32_t resource_id) {
  Complication *c = &s_comp[ci];
  if (!c->icon_layer) return;
  if (c->icon_bitmap) { gbitmap_destroy(c->icon_bitmap); c->icon_bitmap = NULL; }
  c->icon_bitmap = gbitmap_create_with_resource(resource_id);
  bitmap_layer_set_bitmap(c->icon_layer, c->icon_bitmap);
}

// Set a glyph slot (0-2) for a complication to a MININUMBERS index.
// Repositions the full sprite BitmapLayer within its 10px clip layer so that
// the desired glyph (at x = glyph_index * 10 in the sheet) is visible.
static void prv_comp_set_glyph(int ci, int slot, int glyph_index) {
  BitmapLayer *sprite = s_comp[ci].glyph_sprite[slot];
  if (!sprite) return;
  layer_set_frame(bitmap_layer_get_layer(sprite),
                  GRect(-(glyph_index * SPRITE_MINI_ELEMENT_SPACING), 0,
                        SPRITE_MINI_SHEET_W, SPRITE_MINI_GLYPH_HEIGHT));
}

// Complication 0: weather icon + right-justified temperature (no degree sign)
//   single digit:    [blank][blank][N]
//   two digits:      [blank][tens][ones]
//   negative single: [dash][blank][N]
//   negative double: [dash][tens][ones]
static void prv_comp_update_weather(void) {
  bool stale = !s_weather_received_at ||
               (time(NULL) - s_weather_received_at > WEATHER_STALE_SECONDS);
  prv_comp_set_icon(0, stale ? RESOURCE_ID_ICON_CLOUD_ERROR
                              : prv_icon_code_to_resource(s_icon_code_buf));
  if (stale) {
    // No data or data too old — show "--"
    prv_comp_set_glyph(0, 0, COMP_BLANK_INDEX);
    prv_comp_set_glyph(0, 1, COMP_DASH_INDEX);
    prv_comp_set_glyph(0, 2, COMP_DASH_INDEX);
    return;
  }
  int t = s_temp;
  bool neg = (t < 0);
  if (neg) t = -t;
  if (t > 99) t = 99;
  int tens = t / 10;
  int ones = t % 10;
  if (neg) {
    prv_comp_set_glyph(0, 0, COMP_DASH_INDEX);
    prv_comp_set_glyph(0, 1, (t >= 10) ? tens : COMP_BLANK_INDEX);
    prv_comp_set_glyph(0, 2, ones);
  } else if (t >= 10) {
    prv_comp_set_glyph(0, 0, COMP_BLANK_INDEX);
    prv_comp_set_glyph(0, 1, tens);
    prv_comp_set_glyph(0, 2, ones);
  } else {
    prv_comp_set_glyph(0, 0, COMP_BLANK_INDEX);
    prv_comp_set_glyph(0, 1, COMP_BLANK_INDEX);
    prv_comp_set_glyph(0, 2, ones);
  }
}

// Complication 1: blank row — no icon, all blank glyphs
static void prv_comp_update_blank(void) {
  prv_comp_set_glyph(1, 0, COMP_BLANK_INDEX);
  prv_comp_set_glyph(1, 1, COMP_BLANK_INDEX);
  prv_comp_set_glyph(1, 2, COMP_BLANK_INDEX);
}

// Complication 2: bluetooth status in slot 4 (rightmost)
//   Connected:    icon (BTICO) in icon_layer, all glyph slots blank
//   Disconnected: icon_layer hidden, 'k' glyph (index 11) in slot 4 (glyph[2])
static void prv_comp_update_bt(void) {
  prv_comp_set_glyph(2, 0, COMP_BLANK_INDEX);
  prv_comp_set_glyph(2, 1, COMP_BLANK_INDEX);
  if (s_bt_connected) {
    // Show BT icon; hide the slot-4 glyph layer so it doesn't cover the icon
    prv_comp_set_icon(2, RESOURCE_ID_IMAGE_BTICO);
    layer_set_hidden(bitmap_layer_get_layer(s_comp[2].icon_layer), false);
    layer_set_hidden(s_comp[2].glyph_clip[2], true);
  } else {
    // Hide icon; show 'k' glyph in slot 4
    layer_set_hidden(bitmap_layer_get_layer(s_comp[2].icon_layer), true);
    layer_set_hidden(s_comp[2].glyph_clip[2], false);
    prv_comp_set_glyph(2, 2, COMP_K_INDEX);
  }
}

// Complication 3: battery icon + percentage
//   <100%:  [blank][tens][ones]
//   100%:   [1][0][0]
static void prv_comp_update_battery(void) {
  prv_comp_set_icon(3, RESOURCE_ID_IMAGE_BATTERY);
  int level = s_battery_level;
  if (level >= 100) {
    prv_comp_set_glyph(3, 0, 1);
    prv_comp_set_glyph(3, 1, 0);
    prv_comp_set_glyph(3, 2, 0);
  } else {
    prv_comp_set_glyph(3, 0, COMP_BLANK_INDEX);
    prv_comp_set_glyph(3, 1, level / 10);
    prv_comp_set_glyph(3, 2, level % 10);
  }
}

static void prv_update_complications(void) {
  prv_comp_update_weather();
  prv_comp_update_blank();
  prv_comp_update_bt();
  prv_comp_update_battery();
}

static void prv_update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  int hour, minute;
  if (clock_is_24h_style()) {
    hour   = tick_time->tm_hour;
    minute = tick_time->tm_min;
  } else {
    hour = tick_time->tm_hour;
    if (hour == 0)    hour = 12;
    else if (hour > 12) hour -= 12;
    minute = tick_time->tm_min;
  }

  int hour_tens   = hour / 10;
  int hour_ones   = hour % 10;
  int minute_tens = minute / 10;
  int minute_ones = minute % 10;

  if (!clock_is_24h_style() && hour < 10) {
    layer_set_hidden(bitmap_layer_get_layer(s_hour_tens_layer), true);
  } else {
    layer_set_hidden(bitmap_layer_get_layer(s_hour_tens_layer), false);
    set_digit_from_sprite(s_hour_tens_layer, hour_tens, s_time_sprite_bitmap,
                          SPRITE_LARGE_DIGIT_WIDTH, SPRITE_LARGE_DIGIT_HEIGHT,
                          &s_current_hour_tens_bitmap);
  }
  set_digit_from_sprite(s_hour_ones_layer, hour_ones, s_time_sprite_bitmap,
                        SPRITE_LARGE_DIGIT_WIDTH, SPRITE_LARGE_DIGIT_HEIGHT,
                        &s_current_hour_ones_bitmap);
  set_digit_from_sprite(s_minute_tens_layer, minute_tens, s_time_sprite_bitmap,
                        SPRITE_LARGE_DIGIT_WIDTH, SPRITE_LARGE_DIGIT_HEIGHT,
                        &s_current_minute_tens_bitmap);
  set_digit_from_sprite(s_minute_ones_layer, minute_ones, s_time_sprite_bitmap,
                        SPRITE_LARGE_DIGIT_WIDTH, SPRITE_LARGE_DIGIT_HEIGHT,
                        &s_current_minute_ones_bitmap);

  int month = tick_time->tm_mon + 1;
  int day   = tick_time->tm_mday;

  int month_tens = month / 10;
  int month_ones = month % 10;
  int day_tens   = day / 10;
  int day_ones   = day % 10;

  layer_set_hidden(bitmap_layer_get_layer(s_month_tens_layer), false);
  set_digit_from_sprite(s_month_tens_layer, month_tens, s_date_sprite_bitmap,
                        SPRITE_MEDIUM_DIGIT_WIDTH, SPRITE_MEDIUM_DIGIT_HEIGHT,
                        &s_current_month_tens_bitmap);
  set_digit_from_sprite(s_month_ones_layer, month_ones, s_date_sprite_bitmap,
                        SPRITE_MEDIUM_DIGIT_WIDTH, SPRITE_MEDIUM_DIGIT_HEIGHT,
                        &s_current_month_ones_bitmap);

  if (day_tens > 0) {
    layer_set_hidden(bitmap_layer_get_layer(s_day_tens_layer), false);
    set_digit_from_sprite(s_day_tens_layer, day_tens, s_date_sprite_bitmap,
                          SPRITE_MEDIUM_DIGIT_WIDTH, SPRITE_MEDIUM_DIGIT_HEIGHT,
                          &s_current_day_tens_bitmap);
  } else {
    layer_set_hidden(bitmap_layer_get_layer(s_day_tens_layer), true);
  }
  set_digit_from_sprite(s_day_ones_layer, day_ones, s_date_sprite_bitmap,
                        SPRITE_MEDIUM_DIGIT_WIDTH, SPRITE_MEDIUM_DIGIT_HEIGHT,
                        &s_current_day_ones_bitmap);
}

static void prv_update_weather_bar(void) {
  bool stale = !s_weather_received_at ||
               (time(NULL) - s_weather_received_at > WEATHER_STALE_SECONDS);
  if (stale) {
    snprintf(s_humidity_buf, sizeof(s_humidity_buf), "--%%" );
    snprintf(s_hilo_buf,     sizeof(s_hilo_buf),     "--/--");
  } else {
    snprintf(s_humidity_buf, sizeof(s_humidity_buf), "%d%%",      s_humidity);
    snprintf(s_hilo_buf,     sizeof(s_hilo_buf),     "%d/%d",     s_min, s_max);
  }
  if (s_humidity_layer) text_layer_set_text(s_humidity_layer, s_humidity_buf);
  if (s_hilo_layer)     text_layer_set_text(s_hilo_layer,     s_hilo_buf);
}

static void prv_suntime_hide_cb(void *data) {
  s_suntime_hide_timer = NULL;
  if (s_humidity_layer) layer_set_hidden(text_layer_get_layer(s_humidity_layer), true);
  if (s_hilo_layer)     layer_set_hidden(text_layer_get_layer(s_hilo_layer),     true);
  if (s_sunrise_layer)  layer_set_hidden(text_layer_get_layer(s_sunrise_layer),  true);
  if (s_sunset_layer)   layer_set_hidden(text_layer_get_layer(s_sunset_layer),   true);
  if (s_status_layer)   layer_set_hidden(text_layer_get_layer(s_status_layer),   true);
}

static void prv_suntime_show(void) {
  if (s_humidity_layer) layer_set_hidden(text_layer_get_layer(s_humidity_layer), false);
  if (s_hilo_layer)     layer_set_hidden(text_layer_get_layer(s_hilo_layer),     false);
  if (s_sunrise_layer)  layer_set_hidden(text_layer_get_layer(s_sunrise_layer),  false);
  if (s_sunset_layer)   layer_set_hidden(text_layer_get_layer(s_sunset_layer),   false);
  if (s_status_layer)   layer_set_hidden(text_layer_get_layer(s_status_layer),   false);
  if (s_suntime_hide_timer) app_timer_cancel(s_suntime_hide_timer);
  s_suntime_hide_timer = app_timer_register(60 * 1000, prv_suntime_hide_cb, NULL);
}

static void prv_tap_handler(AccelAxisType axis, int32_t direction) {
  prv_suntime_show();
}

// Update sunrise/sunset text and city name status
static void prv_update_suntime_and_status(void) {
  bool sun_stale = !s_weather_received_at ||
                   (time(NULL) - s_weather_received_at > SUNTIME_STALE_SECONDS);
  char rbuf[16] = "--:--", sbuf[16] = "--:--";
  if (!sun_stale && s_sunrise) {
    struct tm *tm = localtime(&s_sunrise);
    if (tm) strftime(rbuf, sizeof(rbuf), "%H:%M", tm);
  }
  if (!sun_stale && s_sunset) {
    struct tm *tm = localtime(&s_sunset);
    if (tm) strftime(sbuf, sizeof(sbuf), "%H:%M", tm);
  }
  snprintf(s_sunrise_buf, sizeof(s_sunrise_buf), "%s", rbuf);
  snprintf(s_sunset_buf,  sizeof(s_sunset_buf),  "%s", sbuf);
  if (s_sunrise_layer) text_layer_set_text(s_sunrise_layer, s_sunrise_buf);
  if (s_sunset_layer)  text_layer_set_text(s_sunset_layer,  s_sunset_buf);

  if (s_status_layer) {
    if (s_city_buf[0]) {
      text_layer_set_text(s_status_layer, s_city_buf);
    } else {
      text_layer_set_text(s_status_layer, "");
    }
  }
}

// Callback from weather module when new data arrives
static void weather_module_cb(const weather_data_t *data, void *ctx) {
  if (!data) return;
  s_temp     = data->temp;
  s_humidity = data->humidity;
  s_min      = data->min;
  s_max      = data->max;
  s_sunrise  = data->sunrise;
  s_sunset   = data->sunset;
  strncpy(s_city_buf, data->city, sizeof(s_city_buf));
  s_city_buf[sizeof(s_city_buf)-1] = '\0';
  if (data->icon_code[0]) {
    strncpy(s_icon_code_buf, data->icon_code, sizeof(s_icon_code_buf));
    s_icon_code_buf[sizeof(s_icon_code_buf)-1] = '\0';
  } else {
    s_icon_code_buf[0] = '\0';
  }
  s_weather_received_at = time(NULL);
  // Persist so data survives watchface process restarts
  persist_write_int(PERSIST_KEY_TEMP,       s_temp);
  persist_write_int(PERSIST_KEY_HUMIDITY,   s_humidity);
  persist_write_int(PERSIST_KEY_MIN,        s_min);
  persist_write_int(PERSIST_KEY_MAX,        s_max);
  persist_write_int(PERSIST_KEY_SUNRISE,    (int32_t)s_sunrise);
  persist_write_int(PERSIST_KEY_SUNSET,     (int32_t)s_sunset);
  persist_write_string(PERSIST_KEY_ICON_CODE, s_icon_code_buf);
  persist_write_int(PERSIST_KEY_WEATHER_AT, (int32_t)s_weather_received_at);
  // Receiving data proves BT is connected — sync icon immediately
  if (!s_bt_connected) {
    s_bt_connected = true;
    prv_comp_update_bt();
  }
  prv_update_complications();
  prv_update_suntime_and_status();
  prv_update_weather_bar();
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *tt = dict_read_first(iter);
  while (tt) {
    if (tt->type == TUPLE_CSTRING) {
      APP_LOG(APP_LOG_LEVEL_INFO, "INBOX key=%lu val=%s", (unsigned long)tt->key, tt->value->cstring);
    } else {
      APP_LOG(APP_LOG_LEVEL_INFO, "INBOX key=%lu int=%ld", (unsigned long)tt->key, (long)tt->value->int32);
    }
    tt = dict_read_next(iter);
  }

  weather_handle_inbox(iter);

  Tuple *t;
  t = dict_find(iter, MESSAGE_KEY_BT_CONNECTED);
  if (t) {
    s_bt_connected = (bool)t->value->int32;
    prv_update_complications();
  }
  t = dict_find(iter, MESSAGE_KEY_BATTERY_LEVEL);
  if (t) {
    s_battery_level = (int)t->value->int32;
    prv_update_complications();
  }

  t = dict_find(iter, MESSAGE_KEY_DARK_MODE);
  if (t) {
    int dm = (t->type == TUPLE_CSTRING) ? atoi(t->value->cstring) : (int)t->value->int32;
    prv_set_dark_mode(dm ? true : false);
    persist_write_int(PERSIST_KEY_DARK_MODE, dm);
  }
  t = dict_find(iter, MESSAGE_KEY_VIBRATE_BT);
  if (t) {
    int vb = (t->type == TUPLE_CSTRING) ? atoi(t->value->cstring) : (int)t->value->int32;
    s_vibrate_bt = vb ? true : false;
    persist_write_int(PERSIST_KEY_VIBRATE_BT, vb);
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
  bool was_connected = s_prev_bt_connected;
  s_prev_bt_connected = connected;
  s_bt_connected = connected;
  prv_comp_update_bt();  // only update BT icon; weather keeps last known data
  if (s_vibrate_bt && (connected != was_connected)) {
    vibes_double_pulse();
  }
  if (!was_connected && connected) {
    if (!weather_request()) {
      APP_LOG(APP_LOG_LEVEL_INFO, "weather_request skipped (cooldown)");
    }
  }
}

static void prv_battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  prv_update_complications();
}

static void prv_sync_bt_state(void) {
  bool connected = connection_service_peek_pebble_app_connection();
  if (connected != s_bt_connected) {
    s_bt_connected = connected;
    prv_comp_update_bt();
  }
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time();
  prv_sync_bt_state();  // poll — connection_service callbacks are unreliable
  if ((tick_time->tm_min % 20) == 0) {
    if (!weather_request()) {
      APP_LOG(APP_LOG_LEVEL_INFO, "weather_request skipped (cooldown, tick)");
    }
  }
}

static void prv_window_appear(Window *window) {
  // Re-check connection state each time the watchface becomes visible,
  // in case the callback was missed while another window was on top.
  bool connected = connection_service_peek_pebble_app_connection();
  if (connected != s_bt_connected) {
    s_bt_connected = connected;
    prv_comp_update_bt();
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, s_dark_mode ? GColorBlack : GColorWhite);

  // ---- Large digit block layout ----
  // Each digit is SPRITE_LARGE_ELEMENT_WIDTH (48px) wide x SPRITE_LARGE_DIGIT_HEIGHT (64px) tall.
  // Two-digit block = BLOCK_W wide (2 digits side by side with optional padding).
  const int DIGIT_W  = SPRITE_LARGE_ELEMENT_WIDTH;
  const int DIGIT_H  = SPRITE_LARGE_DIGIT_HEIGHT;
  const int BLOCK_W  = 96;
  const int BLOCK_GAP = 8;

  // Pad the two digits within the 96px block
  const int TIME_PADDING = (BLOCK_W - (DIGIT_W * 2)) / 2;  // 0px if DIGIT_W=48
  const int LEFT_MARGIN  = (BLOCK_W - (DIGIT_W * 2) - TIME_PADDING) / 2;

  // Center both blocks vertically on screen
  int total_time_h  = (DIGIT_H * 2) + BLOCK_GAP;
  int time_start_y  = (bounds.size.h - total_time_h) / 2;

  int hour_x = 0;
  int hour_y = time_start_y;

  s_hour_tens_layer = bitmap_layer_create(GRect(hour_x + LEFT_MARGIN, hour_y, DIGIT_W, DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_hour_tens_layer));

  s_hour_ones_layer = bitmap_layer_create(GRect(hour_x + LEFT_MARGIN + DIGIT_W + TIME_PADDING, hour_y, DIGIT_W, DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_hour_ones_layer));

  int minute_x = bounds.size.w - BLOCK_W;
  int minute_y = time_start_y + DIGIT_H + BLOCK_GAP;

  s_minute_tens_layer = bitmap_layer_create(GRect(minute_x + LEFT_MARGIN, minute_y, DIGIT_W, DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_minute_tens_layer));

  s_minute_ones_layer = bitmap_layer_create(GRect(minute_x + LEFT_MARGIN + DIGIT_W + TIME_PADDING, minute_y, DIGIT_W, DIGIT_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_minute_ones_layer));

  // ---- Load sprite sheet bitmaps ----
  s_time_sprite_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_BIGNUMBERS_FIXED);
  s_date_sprite_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_MIDINUMBERS_FIXED);
  s_mini_sprite        = gbitmap_create_with_resource(RESOURCE_ID_IMG_MININUMBERS);
  APP_LOG(APP_LOG_LEVEL_INFO, "Sprite loading: time=%p, date=%p, mini=%p",
          s_time_sprite_bitmap, s_date_sprite_bitmap, s_mini_sprite);

  // ---- Load weather fonts (kept for potential future use) ----
  #ifdef RESOURCE_ID_FONT_WEATHER_24
    s_icon_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WEATHER_24));
  #endif
  #ifdef RESOURCE_ID_FONT_WEATHER_12
    s_sky_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WEATHER_12));
  #endif

  // ---- Date digits (medium sprites, right of hour block) ----
  const int DATE_W       = SPRITE_MEDIUM_ELEMENT_WIDTH;
  const int DATE_H       = SPRITE_MEDIUM_DIGIT_HEIGHT;
  const int DATE_PADDING = 4;
  int date_x = BLOCK_W;

  int month_y = hour_y;
  s_month_tens_layer = bitmap_layer_create(GRect(date_x, month_y, DATE_W, DATE_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_month_tens_layer));
  s_month_ones_layer = bitmap_layer_create(GRect(date_x + DATE_W + DATE_PADDING, month_y, DATE_W, DATE_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_month_ones_layer));

  int day_y = hour_y + (DIGIT_H / 2);
  s_day_tens_layer = bitmap_layer_create(GRect(date_x, day_y, DATE_W, DATE_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_day_tens_layer));
  s_day_ones_layer = bitmap_layer_create(GRect(date_x + DATE_W + DATE_PADDING, day_y, DATE_W, DATE_H));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_day_ones_layer));

  // ---- Complications (bottom-left, alongside the minute block) ----
  // 4 rows × COMP_H (16px) = 64px = DIGIT_H, filling the full minute block height.
  // Each row: slot 0 = 10px icon BitmapLayer; slots 1-3 = 10px clip + 130px sprite.
  // Glyphs are vertically centered: (16-13)/2 = 1px top offset within each row.
  int glyph_y_off = (COMP_H - SPRITE_MINI_GLYPH_HEIGHT) / 2;  // 1px

  for (int i = 0; i < COMP_COUNT; i++) {
    int row_y = minute_y + i * COMP_H;
    Complication *c = &s_comp[i];

    // Slot 0: icon BitmapLayer
    c->icon_layer = bitmap_layer_create(
        GRect(0, row_y + glyph_y_off, COMP_SLOT_W, SPRITE_MINI_GLYPH_HEIGHT));
    bitmap_layer_set_background_color(c->icon_layer, GColorClear);
    layer_add_child(window_layer, bitmap_layer_get_layer(c->icon_layer));

    // Slots 1-3: 10px clip Layer + full-sheet BitmapLayer inside
    for (int s = 0; s < 3; s++) {
      // Clip layer: 10px wide, provides clipping to one glyph slot
      c->glyph_clip[s] = layer_create(
          GRect((s + 1) * COMP_SLOT_W, row_y + glyph_y_off,
                COMP_SLOT_W, SPRITE_MINI_GLYPH_HEIGHT));
      layer_add_child(window_layer, c->glyph_clip[s]);

      // Sprite layer: full 130px sheet, positioned so the blank glyph (index 12)
      // is initially aligned with x=0 of the clip layer.
      c->glyph_sprite[s] = bitmap_layer_create(
          GRect(-(COMP_BLANK_INDEX * SPRITE_MINI_ELEMENT_SPACING), 0,
                SPRITE_MINI_SHEET_W, SPRITE_MINI_GLYPH_HEIGHT));
      bitmap_layer_set_bitmap(c->glyph_sprite[s], s_mini_sprite);
      bitmap_layer_set_background_color(c->glyph_sprite[s], GColorClear);
      layer_add_child(c->glyph_clip[s], bitmap_layer_get_layer(c->glyph_sprite[s]));
    }
  }

  // BT complication: move icon_layer to slot 4 (rightmost, x = 3*COMP_SLOT_W)
  layer_set_frame(bitmap_layer_get_layer(s_comp[2].icon_layer),
                  GRect(3 * COMP_SLOT_W, minute_y + 2 * COMP_H + glyph_y_off,
                        COMP_SLOT_W, SPRITE_MINI_GLYPH_HEIGHT));

  // ---- Top weather bar: humidity (left) + high/low (right) ----
  const int TOP_BAR_H      = 14;
  const int TOP_BAR_MARGIN = -4;

  s_humidity_layer = text_layer_create(
      GRect(4, TOP_BAR_MARGIN, bounds.size.w / 2 - 4, TOP_BAR_H));
  text_layer_set_background_color(s_humidity_layer, GColorClear);
  text_layer_set_text_color(s_humidity_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_humidity_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_humidity_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_humidity_layer));

  s_hilo_layer = text_layer_create(
      GRect(bounds.size.w / 2, TOP_BAR_MARGIN, bounds.size.w / 2 - 4, TOP_BAR_H));
  text_layer_set_background_color(s_hilo_layer, GColorClear);
  text_layer_set_text_color(s_hilo_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_hilo_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_hilo_layer, GTextAlignmentRight);
  layer_add_child(window_layer, text_layer_get_layer(s_hilo_layer));

  // ---- Sunrise / sunset / status (bottom row) ----
  const int SUN_HEIGHT    = 14;
  const int STATUS_HEIGHT = 18;
  const int BOTTOM_MARGIN = 2;

  GRect sunrise_frame = GRect(4, bounds.size.h - SUN_HEIGHT - BOTTOM_MARGIN,
                               bounds.size.w / 2 - 4, SUN_HEIGHT);
  GRect sunset_frame  = GRect(bounds.size.w / 2, bounds.size.h - SUN_HEIGHT - BOTTOM_MARGIN,
                               bounds.size.w / 2 - 4, SUN_HEIGHT);

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

  GRect status_frame = GRect(bounds.size.w / 4,
                              bounds.size.h - STATUS_HEIGHT - BOTTOM_MARGIN,
                              bounds.size.w / 2, STATUS_HEIGHT);
  s_status_layer = text_layer_create(status_frame);
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, s_dark_mode ? GColorWhite : GColorBlack);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_text(s_status_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(s_status_layer));

  // Top/bottom info bars hidden by default; shown on wrist shake for 1 minute
  layer_set_hidden(text_layer_get_layer(s_humidity_layer), true);
  layer_set_hidden(text_layer_get_layer(s_hilo_layer),     true);
  layer_set_hidden(text_layer_get_layer(s_sunrise_layer),  true);
  layer_set_hidden(text_layer_get_layer(s_sunset_layer),   true);
  layer_set_hidden(text_layer_get_layer(s_status_layer),   true);

  prv_update_time();
  prv_update_complications();
  prv_update_suntime_and_status();
  prv_update_weather_bar();
}

static void prv_window_unload(Window *window) {
  // Sub-bitmaps from sprite sheets
  if (s_current_hour_tens_bitmap)   gbitmap_destroy(s_current_hour_tens_bitmap);
  if (s_current_hour_ones_bitmap)   gbitmap_destroy(s_current_hour_ones_bitmap);
  if (s_current_minute_tens_bitmap) gbitmap_destroy(s_current_minute_tens_bitmap);
  if (s_current_minute_ones_bitmap) gbitmap_destroy(s_current_minute_ones_bitmap);
  if (s_current_month_tens_bitmap)  gbitmap_destroy(s_current_month_tens_bitmap);
  if (s_current_month_ones_bitmap)  gbitmap_destroy(s_current_month_ones_bitmap);
  if (s_current_day_tens_bitmap)    gbitmap_destroy(s_current_day_tens_bitmap);
  if (s_current_day_ones_bitmap)    gbitmap_destroy(s_current_day_ones_bitmap);

  // Sprite sheets
  if (s_time_sprite_bitmap) gbitmap_destroy(s_time_sprite_bitmap);
  if (s_date_sprite_bitmap) gbitmap_destroy(s_date_sprite_bitmap);
  if (s_mini_sprite)        gbitmap_destroy(s_mini_sprite);

  // Complication layers (destroy children before parents)
  for (int i = 0; i < COMP_COUNT; i++) {
    Complication *c = &s_comp[i];
    if (c->icon_bitmap) { gbitmap_destroy(c->icon_bitmap); c->icon_bitmap = NULL; }
    if (c->icon_layer)  { bitmap_layer_destroy(c->icon_layer); c->icon_layer = NULL; }
    for (int s = 0; s < 3; s++) {
      // Destroy sprite child before clip parent
      if (c->glyph_sprite[s]) { bitmap_layer_destroy(c->glyph_sprite[s]); c->glyph_sprite[s] = NULL; }
      if (c->glyph_clip[s])   { layer_destroy(c->glyph_clip[s]); c->glyph_clip[s] = NULL; }
    }
  }

  // Bitmap layers
  bitmap_layer_destroy(s_hour_tens_layer);
  bitmap_layer_destroy(s_hour_ones_layer);
  bitmap_layer_destroy(s_minute_tens_layer);
  bitmap_layer_destroy(s_minute_ones_layer);
  bitmap_layer_destroy(s_month_tens_layer);
  bitmap_layer_destroy(s_month_ones_layer);
  bitmap_layer_destroy(s_day_tens_layer);
  bitmap_layer_destroy(s_day_ones_layer);

  // Text layers
  text_layer_destroy(s_humidity_layer);
  text_layer_destroy(s_hilo_layer);
  text_layer_destroy(s_sunrise_layer);
  text_layer_destroy(s_sunset_layer);
  text_layer_destroy(s_status_layer);

  // Custom fonts
  #ifdef RESOURCE_ID_FONT_WEATHER_24
    if (s_icon_font) fonts_unload_custom_font(s_icon_font);
  #endif
  #ifdef RESOURCE_ID_FONT_WEATHER_12
    if (s_sky_font) fonts_unload_custom_font(s_sky_font);
  #endif
}

static void prv_set_dark_mode(bool enable) {
  s_dark_mode = enable;
  if (!s_window) return;
  window_set_background_color(s_window, s_dark_mode ? GColorBlack : GColorWhite);
  GColor fg = s_dark_mode ? GColorWhite : GColorBlack;
  if (s_humidity_layer) text_layer_set_text_color(s_humidity_layer, fg);
  if (s_hilo_layer)     text_layer_set_text_color(s_hilo_layer,     fg);
  if (s_sunrise_layer)  text_layer_set_text_color(s_sunrise_layer,  fg);
  if (s_sunset_layer)   text_layer_set_text_color(s_sunset_layer,   fg);
  if (s_status_layer)   text_layer_set_text_color(s_status_layer,   fg);
  prv_update_time();
}

static void prv_init(void) {
  if (persist_exists(PERSIST_KEY_DARK_MODE)) {
    s_dark_mode = persist_read_int(PERSIST_KEY_DARK_MODE) ? true : false;
  }
  if (persist_exists(PERSIST_KEY_VIBRATE_BT)) {
    s_vibrate_bt = persist_read_int(PERSIST_KEY_VIBRATE_BT) ? true : false;
  }
  if (persist_exists(PERSIST_KEY_WEATHER_AT)) {
    s_weather_received_at = (time_t)persist_read_int(PERSIST_KEY_WEATHER_AT);
    s_temp     = persist_read_int(PERSIST_KEY_TEMP);
    s_humidity = persist_read_int(PERSIST_KEY_HUMIDITY);
    s_min      = persist_read_int(PERSIST_KEY_MIN);
    s_max      = persist_read_int(PERSIST_KEY_MAX);
    s_sunrise  = (time_t)persist_read_int(PERSIST_KEY_SUNRISE);
    s_sunset   = (time_t)persist_read_int(PERSIST_KEY_SUNSET);
    persist_read_string(PERSIST_KEY_ICON_CODE, s_icon_code_buf, sizeof(s_icon_code_buf));
  }

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load   = prv_window_load,
    .unload = prv_window_unload,
    .appear = prv_window_appear,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_register_outbox_failed(prv_outbox_failed);
  app_message_register_outbox_sent(prv_outbox_sent);
  app_message_open(256, 256);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  accel_tap_service_subscribe(prv_tap_handler);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = prv_bluetooth_callback
  });
  battery_state_service_subscribe(prv_battery_callback);

  s_prev_bt_connected = connection_service_peek_pebble_app_connection();
  prv_bluetooth_callback(s_prev_bt_connected);
  prv_battery_callback(battery_state_service_peek());

  weather_init(weather_module_cb, NULL);
  weather_start_periodic(20);
}

static void prv_deinit(void) {
  accel_tap_service_unsubscribe();
  if (s_suntime_hide_timer) { app_timer_cancel(s_suntime_hide_timer); s_suntime_hide_timer = NULL; }
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  app_message_deregister_callbacks();
  weather_stop_periodic();
  weather_deinit();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  APP_LOG(APP_LOG_LEVEL_INFO, "watchface1 initialized");
  app_event_loop();
  prv_deinit();
}
