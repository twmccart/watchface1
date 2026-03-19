# BlockFace Watchface — Extracted Image Assets

These images were extracted from the compiled resource pack (`app_resources.pbpack`) of the **BlockFace** watchface (v1.6, by TomHol), targeting the Pebble **diorite** platform (Pebble Time Round). All images are 1-bit PNGs.

---

## Rendering notes

Most images are **1-bit grayscale** — white pixels are the drawn content, black is background. They are designed to be composited onto a black watchface background.

Two images (`IMG_BIGNUMBERS` and `IMG_MIDINUMBERS`) are **1-bit indexed PNGs** with a palette and transparency. In these files, the white digit pixels are marked transparent and the black background is opaque — this is how they were stored for Pebble compositing. Two corrected versions (`IMG_BIGNUMBERS-fixed.png` and `IMG_MIDINUMBERS-fixed.png`) have the transparency inverted so the digit shapes are opaque white on a transparent background, which is more useful for typical compositing workflows.

---

## Sprite sheets

These are horizontal sprite sheets containing all characters in a single image. The watchface slices them at runtime by index.

| File | Dimensions | Description |
|------|------------|-------------|
| `IMG_BIGNUMBERS.png` / `IMG_BIGNUMBERS-fixed.png` | 480×64 | Large digit sprite sheet (source: `num_large.png`). Likely 10 digits (0–9) at 48×64px each. Indexed PNG — use the `-fixed` version for standard compositing. |
| `IMG_MIDNUMBERS.png` / `IMG_MIDINUMBERS-fixed.png` | 240×30 | Medium digit sprite sheet (source: `num_mid.png`). Likely 10 digits at 24×30px each. Indexed PNG — use the `-fixed` version for standard compositing. |
| `IMG_MININUMBERS.png` | 130×13 | Small digit sprite sheet (source: `num_mini.png`). Likely 10 digits at 13×13px each. Grayscale, white digits on black — no transparency correction needed. |
| `IMG_MINITEXTS.png` | 90×156 | Small text/label character sprite sheet (source: `text_mini.png`). Contains letters and/or symbols used for watchface labels (e.g. day, month, AM/PM). Grayscale. |

---

## UI icons

Small standalone icons used in the watchface UI.

| File | Dimensions | Description |
|------|------------|-------------|
| `IMAGE_MENUICON.png` | 25×25 | App menu icon shown in the Pebble launcher. |
| `IMAGE_BTICO.png` | 11×13 | Bluetooth connected indicator icon. |
| `IMAGE_BTICOOFF.png` | 11×13 | Bluetooth disconnected indicator icon. |
| `IMAGE_BATTERY.png` | 10×13 | Battery level indicator icon. |
| `IMAGE_HEART.png` | 10×13 | Heart rate indicator icon. |

---

## Weather icons

All weather icons are **10×13px, 1-bit grayscale**. They correspond to weather condition codes used by the watchface's weather data source.

| File | Condition |
|------|-----------|
| `WEATHER_CLEAR_DAY.png` | Clear sky, daytime |
| `WEATHER_CLEAR_NIGHT.png` | Clear sky, nighttime |
| `WEATHER_PARTLY_CLOUDY_DAY.png` | Partly cloudy, daytime |
| `WEATHER_PARTLY_CLOUDY_NIGHT.png` | Partly cloudy, nighttime |
| `WEATHER_CLOUDY.png` | Overcast / cloudy |
| `WEATHER_RAIN.png` | Rain |
| `WEATHER_DRIZZLE.png` | Drizzle / light rain |
| `WEATHER_SLEET.png` | Sleet |
| `WEATHER_SNOW.png` | Snow |
| `WEATHER_SNOW_SLEET.png` | Snow and sleet mix |
| `WEATHER_RAIN_SNOW.png` | Rain and snow mix |
| `WEATHER_RAIN_SLEET.png` | Rain and sleet mix |
| `WEATHER_THUNDER.png` | Thunderstorm |
| `WEATHER_WIND.png` | Wind / blustery |
| `WEATHER_FOG.png` | Fog / mist |
| `WEATHER_HOT.png` | Extreme heat |
| `WEATHER_COLD.png` | Extreme cold |
| `ICON_CLOUD_ERROR.png` | Weather data unavailable / error |
