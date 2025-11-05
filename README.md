# watchface1

Minimal Pebble watchface based on an older open-source example.

What changed
- Converted project from a button-demo app to a real watchface (`watchapp.watchface` = true in `package.json`).
- Replaced the sample `src/c/watchface1.c` with a minimal watchface that displays the current time and updates every minute.

Build

This project uses the Pebble SDK Waf build rules present in `wscript`.

To build (on a system with the Pebble SDK installed and `pebble` command available):

```bash
pebble build
```

To install on a connected device or emulator:

```bash
pebble install --emulator diorite
```

Notes and next steps
- I kept the project compatible with SDK version 3 targets listed in `package.json`.
- If you want a different layout, fonts, or complications (battery, date), tell me which features to add.
- If you don't have the Pebble SDK locally, I can help set up a Docker-based build environment.
