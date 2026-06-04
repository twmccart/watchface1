#include "settings.h"
#include "chime.h"

// Add new rows here as settings grow; each row can push its own sub-window.

typedef enum {
  SETTINGS_ROW_CHIME = 0,
  SETTINGS_ROW_COUNT
} SettingsRow;

static Window    *s_window;
static MenuLayer *s_menu_layer;

static uint16_t prv_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  return SETTINGS_ROW_COUNT;
}

static int16_t prv_header_height(MenuLayer *ml, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void prv_draw_header(GContext *gc, const Layer *layer,
                             uint16_t section, void *ctx) {
  menu_cell_basic_header_draw(gc, layer, "Settings");
}

static void prv_draw_row(GContext *gc, const Layer *layer,
                          MenuIndex *idx, void *ctx) {
  switch (idx->row) {
    case SETTINGS_ROW_CHIME:
      menu_cell_basic_draw(gc, layer, "Hourly Chime", NULL, NULL);
      break;
  }
}

static void prv_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  switch (idx->row) {
    case SETTINGS_ROW_CHIME:
      chime_open_settings();
      break;
  }
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows      = prv_num_rows,
    .get_header_height = prv_header_height,
    .draw_header       = prv_draw_header,
    .draw_row          = prv_draw_row,
    .select_click      = prv_select,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void prv_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void settings_open(void) {
  if (s_window) return;
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);
}

void settings_deinit(void) {
  if (s_window) window_stack_remove(s_window, false);
}
