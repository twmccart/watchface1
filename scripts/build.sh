#!/usr/bin/env bash
# Wrapper around pebble build that deletes stale auto-generated message key
# files before building. The SDK's dependency tracking doesn't detect changes
# to package.json messageKeys, so these files can silently go stale when keys
# are added or reordered.
set -e

rm -f build/js/message_keys.json \
      build/include/message_keys.auto.h \
      build/src/message_keys.auto.c

pebble build "$@"
