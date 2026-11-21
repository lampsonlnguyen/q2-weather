# Changelog

## Unreleased — q2-pod theme look

### Changed
- **q2-pod theme inheritance:** Every screen is now a q2-pod list — 26 px status bar, 32 px rows, the active theme's own selection painter (Minimal's card, Classic's gloss stops, Modern's accent bar), chevron on the selected row, settings values right-aligned. Scheme values are ported from q2-pod itself (`src/q2pod_scheme.c`: `ACCENTS` tones from `patch/offsets.inc`, Classic Blue's band and gloss bar from `assets/themes/classic-light/theme.xml`, the HSV gloss derivation from `tools/theme.py`), resolved from the already-parsed theme/accent/Light Mode, so the app keeps following live theme changes.
- **Text-only conditions:** All condition sprites are deleted — `assets/sprites/`, `tools/pack_sprites.py`, `tools/draw_weather_icons.py`, and the sprite atlas. Conditions are text with temperature/high/low/precipitation. The forecast is seven list rows instead of 45 px icon columns; the day detail is a list page; the keyboard keeps its QWERTY layout and behavior with theme selection and a **DEL** text key instead of the pixel-drawn backspace arrow.
- **Fonts:** The build-time glyph rasterizer stays (font engine, not graphics) and is now packed by `tools/pack_fonts.py` into `build/weather_fonts.inc`. The launcher `assets/icon.png` is unchanged and is still the app's only image.
- q2-pod's native art rows are unaffected: `src/art.c` still publishes JPEGs from `.weather/icons/` for the firmware's art rows.

### Preserved
- Weather data, providers, fallbacks, caching, controls, pages, and the service helper are unchanged; presentation only.
- On-device verification is still outstanding (the Q2 Pod 1.8.8 emulator crashes at the boot logo); host renders cover every page × Minimal/Classic/Modern × light/dark via `tests/test_render.c`.

## v1.3.0 — Independent weather and location fallback providers (2026-11-20)

### Added
- **Weather API fallback:** Use [Open-Meteo](https://open-meteo.com/en/docs) first. If its forecast request fails or its daily forecast is incomplete, use the independent [MET Norway Locationforecast 2.0](https://api.met.no/weatherapi/locationforecast/2.0/documentation) API.
- **Current Location fallback:** Try `ipwho.is`, then `ipapi.co` for approximate public-IP location.
- **Manual Home City:** Users can choose Current Location when unconfigured or open **Settings → Set Home City** to select a location manually. Manual Home is saved across restarts and isn't overwritten by automatic IP location. Remove `.weather/manual-home` to return to auto-detection.
- **City-search fallback:** Open-Meteo geocoding is primary; OpenStreetMap Nominatim is the fallback for user-initiated manual city searches.

### Fixed
- **Saved Location independence:** The saved location uses its existing coordinates to request forecasts directly, before any current-location detection. Failed IP geolocation no longer blocks a saved city's weather.
- **Connectivity preflight:** Removed the requirement that `ipwho.is` succeed before fetching weather.
- **Cached data:** Keep the existing stored forecast when network/provider requests fail.

### Preserved
- Stable v1.2.2 interface, click-wheel navigation to Settings, and ABC/123 QWERTY city keyboard.
- Smaller, centered backspace arrow with unchanged touch target.
- Existing `.weather/state` data and Library → Weather installation layout.

### Provider differences and limits
- MET Norway time-series forecasts are grouped into **UTC** daily highs/lows. They may not match Open-Meteo's local-time daily boundaries exactly.
- Precipitation probability is shown as unavailable if the alternate provider does not supply it.
- If both forecast APIs are unavailable, the app cannot display newly downloaded weather; offline cached weather remains visible.
- City search needs either the primary or fallback geocoder to be reachable.
- Q2 hardware testing is still required; automated host checks and MIPS build passed for the feature branch.

### Attribution
- MET Norway fallback forecasts © Norwegian Meteorological Institute, CC BY 4.0.
- Backup geocoding data © OpenStreetMap contributors, ODbL.

## v1.2.2 — Wheel Settings and numeric keyboard

- Click-wheel can highlight and open Settings from the initial location list.
- ABC/123 keyboard mode adds digits and common punctuation.
- Separate Space, Clear All, backspace and Search controls, retaining the Q2 rounded-screen safe area.
