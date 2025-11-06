/* weather.h
 * Minimal weather module API (draft)
 *
 * Responsibilities:
 *  - hold latest weather state (temp, min/max, humidity, sunrise/sunset, sky_code, city)
 *  - parse incoming AppMessage payloads for weather keys
 *  - request weather refresh via outbox
 *  - notify a registered callback on updates
 */

#pragma once

#include <pebble.h>

typedef struct {
  int temp;
  int humidity;
  int min;
  int max;
  time_t sunrise;
  time_t sunset;
  int sky_code; /* 0=clear,1=clouds,2=precip */
  char city[32];
  char glyph[8]; /* UTF-8 glyph string (null-terminated) from WeatherIcons font */
  char icon_code[4]; /* OWM icon code like '01d' or '04n' (3 chars + NUL) */
} weather_data_t;

typedef void (*weather_update_callback)(const weather_data_t *data, void *ctx);

/* Initialize the weather module. Provide an optional callback that will be
 * invoked whenever parsed weather data changes. The module does not start any
 * background threads; it only reacts to calls to weather_handle_inbox().
 */
void weather_init(weather_update_callback cb, void *ctx);

/* Deinitialize the weather module. */
void weather_deinit(void);

/* Handle an incoming AppMessage dictionary containing weather keys. Call this
 * from the global inbox handler to let the module parse relevant keys.
 */
void weather_handle_inbox(DictionaryIterator *iter);

/* Request a weather refresh via AppMessage outbox. This writes a small
 * request payload the companion recognizes.
 */
/* Request a weather refresh. This function enforces an internal cooldown and
 * returns true if a request was actually sent, or false if skipped due to
 * cooldown. Use weather_force_request() to bypass the cooldown.
 */
bool weather_request(void);

/* Force a weather request, ignoring any cooldown. */
void weather_force_request(void);

/* Periodic polling control: start/stop periodic weather requests.
 * weather_start_periodic(minutes): subscribe to minute ticks and request
 * every `minutes` minutes (e.g. 20). Calling start will immediately
 * trigger a forced request. Passing 0 is a no-op.
 */
void weather_start_periodic(uint16_t minutes);
void weather_stop_periodic(void);

/* Query whether periodic polling is enabled. */
bool weather_is_periodic_enabled(void);

/* Accessor for the current weather snapshot. The pointer is owned by the
 * module and remains valid until deinit or the next update.
 */
const weather_data_t *weather_get(void);

/* Run a small sample test: populate the module with sample values and invoke
 * the update callback. Useful for unit-testing the UI without the companion.
 */
void weather_run_sample_test(void);
