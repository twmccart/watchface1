/* chime.c — Hourly chime module.
 *
 * Settings arrive via chime_handle_inbox() from the Clay phone-side config.
 * Persist keys 20-26 are reserved for this module (watchface1.c uses 1-10).
 *
 * quiet_time_is_active() reflects the Pebble watch's own Quiet Time mode
 * (set on the watch or via the Pebble phone app), not the phone's DND.
 * Suppression of mirrored phone notifications during DND is handled by the
 * Pebble phone app and requires no action here.
 *
 * WARNING: do not add #define fallbacks for MESSAGE_KEY_* here. The SDK
 * declares these as extern uint32_t in message_keys.auto.h; a #define always
 * wins over an extern declaration and will silently use the wrong key number.
 * If the build fails with undeclared MESSAGE_KEY_*, delete the stale
 * build/include/message_keys.auto.h and run scripts/build.sh to regenerate.
 */
#include "chime.h"
#include "message_keys.auto.h"

// Persist keys — start at 20 to avoid collisions with watchface1.c (keys 1-10).
#define KEY_CHIME_ENABLED   20
#define KEY_VIBRATE_ENABLED 21
#define KEY_QUIET_ENABLED   22
#define KEY_QUIET_FROM      23
#define KEY_QUIET_TO        24
#define KEY_CHIME_ON_SHAKE  25
#define KEY_CHIME_RESPECT_QT 26

#define CHIME_GAIN 30.0f

static bool s_chime_enabled;
static bool s_vibrate_enabled;
static bool s_quiet_enabled;
static int  s_quiet_from;
static int  s_quiet_to;
static bool s_chime_on_shake;
static bool s_respect_quiet_time;

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
  s_chime_on_shake  = persist_exists(KEY_CHIME_ON_SHAKE)
      ? persist_read_bool(KEY_CHIME_ON_SHAKE) : false;
  s_respect_quiet_time = persist_exists(KEY_CHIME_RESPECT_QT)
      ? persist_read_bool(KEY_CHIME_RESPECT_QT) : true;
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

void chime_init(void) {
  prv_load_settings();
}

void chime_handle_inbox(DictionaryIterator *iter) {
  Tuple *t;
  t = dict_find(iter, MESSAGE_KEY_CHIME_ENABLED);
  if (t) {
    s_chime_enabled = (bool)t->value->int32;
    persist_write_bool(KEY_CHIME_ENABLED, s_chime_enabled);
  }
  t = dict_find(iter, MESSAGE_KEY_CHIME_VIBRATE);
  if (t) {
    s_vibrate_enabled = (bool)t->value->int32;
    persist_write_bool(KEY_VIBRATE_ENABLED, s_vibrate_enabled);
  }
  t = dict_find(iter, MESSAGE_KEY_QUIET_ENABLED);
  if (t) {
    s_quiet_enabled = (bool)t->value->int32;
    persist_write_bool(KEY_QUIET_ENABLED, s_quiet_enabled);
  }
  t = dict_find(iter, MESSAGE_KEY_QUIET_FROM);
  if (t) {
    s_quiet_from = (int)t->value->int32;
    persist_write_int(KEY_QUIET_FROM, s_quiet_from);
  }
  t = dict_find(iter, MESSAGE_KEY_QUIET_TO);
  if (t) {
    s_quiet_to = (int)t->value->int32;
    persist_write_int(KEY_QUIET_TO, s_quiet_to);
  }
  t = dict_find(iter, MESSAGE_KEY_CHIME_ON_SHAKE);
  if (t) {
    s_chime_on_shake = (bool)t->value->int32;
    persist_write_bool(KEY_CHIME_ON_SHAKE, s_chime_on_shake);
  }
  t = dict_find(iter, MESSAGE_KEY_CHIME_RESPECT_QT);
  if (t) {
    s_respect_quiet_time = (bool)t->value->int32;
    persist_write_bool(KEY_CHIME_RESPECT_QT, s_respect_quiet_time);
  }
}

void chime_on_tap(void) {
  if (s_chime_on_shake) {
    if (s_respect_quiet_time && quiet_time_is_active()) return;
    prv_play_chime();
  }
}

void chime_tick(struct tm *tick_time) {
  if (tick_time->tm_min != 0) return;
  if (!s_chime_enabled && !s_vibrate_enabled) return;
  if (prv_in_quiet_hours(tick_time->tm_hour)) return;
  if (s_respect_quiet_time && quiet_time_is_active()) return;
  prv_play_chime();
}
