# BlockFace Watchface — Extracted Image Assets (emery)

These images were extracted from the compiled resource pack (`app_resources.pbpack`) of the **BlockFace** watchface (v1.6, by TomHol), targeting the Pebble **emery** platform (Pebble Time 2). All images are PNGs.

---

## Rendering notes

Emery is a color display (200×228px), so many images that were 1-bit grayscale on diorite are now **indexed color PNGs**. In indexed files, the Pebble SDK encodes transparency inverted: foreground/content pixels are transparent (alpha=0) and the background is opaque — intentional for Pebble's compositor.

---

## Sprite sheets

These are horizontal sprite sheets containing all characters in a single image. The watchface slices them at runtime by index.

| File | Dimensions | Description |
|------|------------|-------------|
| `IMG_BIGNUMBERS.png` | 660×88 | Large digit sprite sheet (source: `num_large.png`). 10 digits (0–9) at 66×88px each. Grayscale. |
| `IMG_MIDNUMBERS.png` | 330×41 | Medium digit sprite sheet (source: `num_mid.png`). 11 frames (10 digits plus a blank) at 30×41px each. Grayscale. |
| `IMG_MININUMBERS.png` | 200×20 | Small digit sprite sheet (source: `num_mini.png`). Same character set as the diorite version: 10 digits, plus a dash ('-'), plus a 'k', followed by a blank. Grayscale. |
| `IMG_MINITEXTS.png` | 139×240 | Small text/label character sprite sheet (source: `text_mini.png`). Contains letters and/or symbols used for watchface labels (e.g. day, month, AM/PM). Grayscale. |

---

## UI icons

Small standalone icons used in the watchface UI.

| File | Dimensions | Description |
|------|------------|-------------|
| `IMAGE_MENUICON.png` | 25×25 | App menu icon shown in the Pebble launcher. Grayscale. |
| `IMAGE_BTICO.png` | 10×15 | Bluetooth connected indicator icon. Grayscale. |
| `IMAGE_BTICOOFF.png` | 10×15 | Bluetooth disconnected indicator icon. Grayscale. |
| `IMAGE_BATTERY.png` | 13×20 | Battery level indicator icon. Grayscale. |
| `IMAGE_HEART.png` | 15×20 | Heart rate indicator icon. Indexed PNG. |

---

## Weather icons

All weather icons are **15×20px**. Many are **indexed color PNGs** (emery's color display variants, sourced from `-200w.png` files). The remainder are 1-bit grayscale.

| File | Type | Condition |
|------|------|-----------|
| `WEATHER_CLEAR_DAY.png` | Indexed | Clear sky, daytime |
| `WEATHER_CLEAR_NIGHT.png` | Indexed | Clear sky, nighttime |
| `WEATHER_PARTLY_CLOUDY_DAY.png` | Indexed | Partly cloudy, daytime |
| `WEATHER_PARTLY_CLOUDY_NIGHT.png` | Indexed | Partly cloudy, nighttime |
| `WEATHER_CLOUDY.png` | Grayscale | Overcast / cloudy |
| `WEATHER_RAIN.png` | Indexed | Rain |
| `WEATHER_DRIZZLE.png` | Indexed | Drizzle / light rain |
| `WEATHER_SLEET.png` | Grayscale | Sleet |
| `WEATHER_SNOW.png` | Grayscale | Snow |
| `WEATHER_SNOW_SLEET.png` | Grayscale | Snow and sleet mix |
| `WEATHER_RAIN_SNOW.png` | Indexed | Rain and snow mix |
| `WEATHER_RAIN_SLEET.png` | Indexed | Rain and sleet mix |
| `WEATHER_THUNDER.png` | Indexed | Thunderstorm |
| `WEATHER_WIND.png` | Grayscale | Wind / blustery |
| `WEATHER_FOG.png` | Grayscale | Fog / mist |
| `WEATHER_HOT.png` | Indexed | Extreme heat |
| `WEATHER_COLD.png` | Indexed | Extreme cold |
| `ICON_CLOUD_ERROR.png` | Indexed | Weather data unavailable / error |
