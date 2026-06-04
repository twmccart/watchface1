#pragma once

#include <pebble.h>

/* Initialize the chime module: loads persisted settings. */
void chime_init(void);

/* Call from the watchface tick handler (MINUTE_UNIT). Fires the chime when
 * tm_min == 0, subject to the user's enabled/quiet-hours settings. */
void chime_tick(struct tm *tick_time);

/* Handle incoming AppMessage keys for chime settings. Call from the global
 * inbox handler alongside weather_handle_inbox(). */
void chime_handle_inbox(DictionaryIterator *iter);

/* Call from the accel tap handler. Plays the chime if "Chime on shake" is on. */
void chime_on_tap(void);
