#pragma once

#include <pebble.h>

/* Push the top-level settings menu onto the window stack. */
void settings_open(void);

/* Remove the settings window if it is open. Call from prv_deinit. */
void settings_deinit(void);
