#pragma once

#include <pebble.h>

/* Initialize the chime module: loads persisted settings. */
void chime_init(void);

/* Deinitialize: cleans up any open windows. */
void chime_deinit(void);

/* Call from the watchface tick handler (MINUTE_UNIT). Fires the chime when
 * tm_min == 0, subject to the user's enabled/quiet-hours settings. */
void chime_tick(struct tm *tick_time);

/* Handle incoming AppMessage keys for chime settings. Call from the global
 * inbox handler alongside weather_handle_inbox(). */
void chime_handle_inbox(DictionaryIterator *iter);

/* Push the on-watch settings menu onto the window stack. */
void chime_open_settings(void);
