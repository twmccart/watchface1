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
#define KEY_CHIME_VOLUME     27

// Audio buffer pre-loaded at init so resource_load never blocks an event handler.
// s_playing is a separate flag because s_audio_buf is always non-NULL after init.
static int8_t *s_audio_buf  = NULL;
static size_t  s_audio_size = 0;
static bool    s_playing    = false;
static int     s_last_chime_hour = -1;

// Scratch buffer for volume scaling: 8-bit source samples are widened to
// 16-bit here so attenuation keeps its precision (scaling in the 8-bit domain
// leaves ~7 effective bits at volume 50 and sounds gritty). 4096 samples =
// 8192 bytes, the known-safe speaker_stream_write size.
#define CHUNK_SAMPLES 4096
static int16_t s_chunk[CHUNK_SAMPLES];

// speaker_stream_close() drains remaining data asynchronously; this callback
// fires when the hardware is done so we can allow the next play.
static void prv_chime_finished(SpeakerFinishReason reason, void *ctx) {
  (void)reason; (void)ctx;
  s_playing = false;
}

static void prv_play_chime(void);

// Defer playback out of the tap handler via a timer so the handler returns
// immediately — calling prv_play_chime() directly causes "not responding".
static void prv_play_chime_timer_cb(void *ctx) {
  prv_play_chime();
}

static bool s_chime_enabled;
static bool s_vibrate_enabled;
static bool s_quiet_enabled;
static int  s_quiet_from;
static int  s_quiet_to;
static bool s_chime_on_shake;
static bool s_respect_quiet_time;
static int  s_volume;

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
  s_volume             = persist_exists(KEY_CHIME_VOLUME)
      ? persist_read_int(KEY_CHIME_VOLUME) : 100;
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
  if (s_chime_enabled && s_audio_buf && s_volume > 0 && !s_playing) {
    s_playing = true;
    speaker_set_finish_callback(prv_chime_finished, NULL);
    // 16kHz: casio.raw has most energy above 4kHz, which 8kHz sampling discards
    // entirely (peak was 4/127 at 8kHz vs 115/127 at 16kHz).
    // File was re-encoded: ffmpeg -i casio.ogg -ar 16000 -ac 1 -f s8 -af "volume=7.3dB"
    //
    // Hardware volume stays at 100; the slider is applied in software while
    // widening to 16-bit, so low settings don't quantize the 8-bit samples.
    // The slider is squared first: hardware volume is linear in amplitude,
    // which makes 50 sound barely quieter than 100.
    speaker_set_volume(100);
    if (speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, 100)) {
      const int32_t amp = ((int32_t)s_volume * s_volume) / 100;  // 0-100
      const int8_t *cursor = s_audio_buf;
      uint32_t remaining = s_audio_size;  // in source samples (1 byte each)
      while (remaining > 0) {
        uint32_t n = remaining < CHUNK_SAMPLES ? remaining : CHUNK_SAMPLES;
        for (uint32_t i = 0; i < n; i++) {
          // Max magnitude: 127 * 256 * 100 / 100 = 32512, within int16_t.
          s_chunk[i] = (int16_t)(((int32_t)cursor[i] * 256 * amp) / 100);
        }
        // Must write in chunks — passing a full >8192-byte buffer in one call
        // causes a firmware crash (stack corruption, App fault LR:???).
        uint32_t written = speaker_stream_write(s_chunk, n * 2);
        // On a partial write the unconsumed tail is re-converted next pass.
        // Assumes the firmware consumes whole 16-bit samples (written even).
        cursor += written / 2;
        remaining -= written / 2;
        if (written == 0) psleep(5);
      }
      speaker_stream_close();
    } else {
      s_playing = false;
    }
  }
}

void chime_init(void) {
  prv_load_settings();
  // Pre-load audio (~13KB) once at startup. resource_load in an event handler
  // blocks the app task long enough to trigger "not responding" on the watch.
  ResHandle handle = resource_get_handle(RESOURCE_ID_CHIME);
  s_audio_size = resource_size(handle);
  s_audio_buf = malloc(s_audio_size);
  if (s_audio_buf) {
    resource_load(handle, (uint8_t *)s_audio_buf, s_audio_size);
  }
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
  t = dict_find(iter, MESSAGE_KEY_CHIME_VOLUME);
  if (t) {
    s_volume = (int)t->value->int32;
    persist_write_int(KEY_CHIME_VOLUME, s_volume);
  }
}

void chime_on_tap(void) {
  if (s_chime_on_shake) {
    if (s_respect_quiet_time && quiet_time_is_active()) return;
    app_timer_register(1, prv_play_chime_timer_cb, NULL);
  }
}

void chime_tick(struct tm *tick_time) {
  if (tick_time->tm_min != 0) return;
  if (tick_time->tm_hour == s_last_chime_hour) return;
  if (!s_chime_enabled && !s_vibrate_enabled) return;
  if (prv_in_quiet_hours(tick_time->tm_hour)) return;
  if (s_respect_quiet_time && quiet_time_is_active()) return;
  s_last_chime_hour = tick_time->tm_hour;
  prv_play_chime();
}
