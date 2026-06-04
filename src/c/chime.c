#include "chime.h"
#include "message_keys.auto.h"


// Persist keys — start at 20 to avoid collisions with watchface1.c (keys 1-10).
#define KEY_CHIME_ENABLED   20
#define KEY_VIBRATE_ENABLED 21
#define KEY_QUIET_ENABLED   22
#define KEY_QUIET_FROM      23
#define KEY_QUIET_TO        24

#define CHIME_GAIN 20.0f

static bool s_chime_enabled;
static bool s_vibrate_enabled;
static bool s_quiet_enabled;
static int  s_quiet_from;
static int  s_quiet_to;

static MenuLayer *s_menu_layer;

static void prv_load_settings(void) {
  s_chime_enabled   = persist_exists(KEY_CHIME_ENABLED)
      ? persist_read_bool(KEY_CHIME_ENABLED) : true;
  s_vibrate_enabled = persist_exists(KEY_VIBRATE_ENABLED)
      ? persist_read_bool(KEY_VIBRATE_ENABLED) : false;
  s_quiet_enabled   = persist_exists(KEY_QUIET_ENABLED)
      ? persist_read_bool(KEY_QUIET_ENABLED) : false;
  s_quiet_from      = persist_exists(KEY_QUIET_FROM)
      ? persist_read_int(KEY_QUIET_FROM) : 22;
  s_quiet_to        = persist_exists(KEY_QUIET_TO)
      ? persist_read_int(KEY_QUIET_TO) : 7;
}

static bool prv_in_quiet_hours(int hour) {
  if (!s_quiet_enabled || s_quiet_from == s_quiet_to) return false;
  if (s_quiet_from < s_quiet_to) {
    return hour >= s_quiet_from && hour < s_quiet_to;
  }
  // Wraps midnight (e.g. 22:00–07:00)
  return hour >= s_quiet_from || hour < s_quiet_to;
}

static void prv_play_chime(void) {
  if (s_vibrate_enabled) {
    vibes_short_pulse();
  }
  if (s_chime_enabled) {
    ResHandle handle = resource_get_handle(RESOURCE_ID_CHIME);
    size_t size = resource_size(handle);
    int8_t *buf = malloc(size);
    if (!buf) return;
    resource_load(handle, (uint8_t *)buf, size);

    for (size_t i = 0; i < size; i++) {
      int32_t s = (int32_t)(buf[i] * CHIME_GAIN);
      if (s >  127) s =  127;
      if (s < -128) s = -128;
      buf[i] = (int8_t)s;
    }

    speaker_set_volume(100);
    if (speaker_stream_open(SpeakerPcmFormat_8kHz_8bit, 100)) {
      const int8_t *cursor = buf;
      uint32_t remaining = size;
      while (remaining > 0) {
        uint32_t written = speaker_stream_write(cursor, remaining);
        cursor += written;
        remaining -= written;
        if (written == 0) psleep(5);
      }
      speaker_stream_close();
      psleep((int32_t)(size / 8) + 50);
    }
    free(buf);
  }
}

// ---- Public API ----

void chime_init(void) {
  prv_load_settings();
}

void chime_handle_inbox(DictionaryIterator *iter) {
  Tuple *t;
  t = dict_find(iter, MESSAGE_KEY_CHIME_ENABLED);
  if (t) {
    s_chime_enabled = (bool)t->value->int32;
    persist_write_bool(KEY_CHIME_ENABLED, s_chime_enabled);
    if (s_menu_layer) menu_layer_reload_data(s_menu_layer);
  }
  t = dict_find(iter, MESSAGE_KEY_CHIME_VIBRATE);
  if (t) {
    s_vibrate_enabled = (bool)t->value->int32;
    persist_write_bool(KEY_VIBRATE_ENABLED, s_vibrate_enabled);
    if (s_menu_layer) menu_layer_reload_data(s_menu_layer);
  }
  t = dict_find(iter, MESSAGE_KEY_QUIET_ENABLED);
  if (t) {
    s_quiet_enabled = (bool)t->value->int32;
    persist_write_bool(KEY_QUIET_ENABLED, s_quiet_enabled);
    if (s_menu_layer) menu_layer_reload_data(s_menu_layer);
  }
  t = dict_find(iter, MESSAGE_KEY_QUIET_FROM);
  if (t) {
    s_quiet_from = (int)t->value->int32;
    persist_write_int(KEY_QUIET_FROM, s_quiet_from);
    if (s_menu_layer) menu_layer_reload_data(s_menu_layer);
  }
  t = dict_find(iter, MESSAGE_KEY_QUIET_TO);
  if (t) {
    s_quiet_to = (int)t->value->int32;
    persist_write_int(KEY_QUIET_TO, s_quiet_to);
    if (s_menu_layer) menu_layer_reload_data(s_menu_layer);
  }
}

void chime_tick(struct tm *tick_time) {
  if (tick_time->tm_min != 0) return;
  if (!s_chime_enabled && !s_vibrate_enabled) return;
  if (prv_in_quiet_hours(tick_time->tm_hour)) return;
  prv_play_chime();
}

// ---- Settings menu ----

typedef enum {
  ROW_CHIME = 0,
  ROW_VIBRATE,
  ROW_QUIET,
  ROW_QUIET_FROM,
  ROW_QUIET_TO,
  ROW_TEST,
  ROW_COUNT
} Row;

static Window       *s_settings_window;
static NumberWindow *s_number_window;
static int           s_editing_row;

static uint16_t prv_get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  return ROW_COUNT;
}

static int16_t prv_get_header_height(MenuLayer *ml, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void prv_draw_header(GContext *gc, const Layer *layer,
                             uint16_t section, void *ctx) {
  menu_cell_basic_header_draw(gc, layer, "Hourly Chime");
}

static void prv_draw_row(GContext *gc, const Layer *layer,
                          MenuIndex *idx, void *ctx) {
  char value[8];
  switch (idx->row) {
    case ROW_CHIME:
      menu_cell_basic_draw(gc, layer, "Chime",
                           s_chime_enabled ? "On" : "Off", NULL);
      break;
    case ROW_VIBRATE:
      menu_cell_basic_draw(gc, layer, "Vibrate",
                           s_vibrate_enabled ? "On" : "Off", NULL);
      break;
    case ROW_QUIET:
      menu_cell_basic_draw(gc, layer, "Quiet Hours",
                           s_quiet_enabled ? "On" : "Off", NULL);
      break;
    case ROW_QUIET_FROM:
      snprintf(value, sizeof(value), "%02d:00", s_quiet_from);
      menu_cell_basic_draw(gc, layer, "Quiet From", value, NULL);
      break;
    case ROW_QUIET_TO:
      snprintf(value, sizeof(value), "%02d:00", s_quiet_to);
      menu_cell_basic_draw(gc, layer, "Quiet To", value, NULL);
      break;
    case ROW_TEST:
      menu_cell_basic_draw(gc, layer, "Test Chime", NULL, NULL);
      break;
  }
}

static void prv_number_selected(NumberWindow *nw, void *ctx) {
  int value = number_window_get_value(nw);
  if (s_editing_row == ROW_QUIET_FROM) {
    s_quiet_from = value;
    persist_write_int(KEY_QUIET_FROM, value);
  } else {
    s_quiet_to = value;
    persist_write_int(KEY_QUIET_TO, value);
  }
  window_stack_pop(true);
  menu_layer_reload_data(s_menu_layer);
}

static void prv_open_hour_picker(int row, const char *label, int initial) {
  s_editing_row = row;
  static const NumberWindowCallbacks cbs = { .selected = prv_number_selected };
  if (s_number_window) number_window_destroy(s_number_window);
  s_number_window = number_window_create(label, cbs, NULL);
  number_window_set_min(s_number_window, 0);
  number_window_set_max(s_number_window, 23);
  number_window_set_step_size(s_number_window, 1);
  number_window_set_value(s_number_window, initial);
  window_stack_push(number_window_get_window(s_number_window), true);
}

static void prv_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  switch (idx->row) {
    case ROW_CHIME:
      s_chime_enabled = !s_chime_enabled;
      persist_write_bool(KEY_CHIME_ENABLED, s_chime_enabled);
      menu_layer_reload_data(ml);
      break;
    case ROW_VIBRATE:
      s_vibrate_enabled = !s_vibrate_enabled;
      persist_write_bool(KEY_VIBRATE_ENABLED, s_vibrate_enabled);
      menu_layer_reload_data(ml);
      break;
    case ROW_QUIET:
      s_quiet_enabled = !s_quiet_enabled;
      persist_write_bool(KEY_QUIET_ENABLED, s_quiet_enabled);
      menu_layer_reload_data(ml);
      break;
    case ROW_QUIET_FROM:
      prv_open_hour_picker(ROW_QUIET_FROM, "Quiet From", s_quiet_from);
      break;
    case ROW_QUIET_TO:
      prv_open_hour_picker(ROW_QUIET_TO, "Quiet To", s_quiet_to);
      break;
    case ROW_TEST:
      prv_play_chime();
      break;
  }
}

static void prv_settings_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows      = prv_get_num_rows,
    .get_header_height = prv_get_header_height,
    .draw_header       = prv_draw_header,
    .draw_row          = prv_draw_row,
    .select_click      = prv_select,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void prv_settings_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
  if (s_number_window) {
    number_window_destroy(s_number_window);
    s_number_window = NULL;
  }
  window_destroy(s_settings_window);
  s_settings_window = NULL;
}

void chime_open_settings(void) {
  if (s_settings_window) return;
  s_settings_window = window_create();
  window_set_window_handlers(s_settings_window, (WindowHandlers){
    .load   = prv_settings_window_load,
    .unload = prv_settings_window_unload,
  });
  window_stack_push(s_settings_window, true);
}

void chime_deinit(void) {
  if (s_settings_window) {
    window_stack_remove(s_settings_window, false);
  }
  // prv_settings_window_unload handles cleanup via the stack removal
}
