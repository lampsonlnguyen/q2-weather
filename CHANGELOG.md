# Changelog

## Unreleased: a Q2 Pod card app

### Changed
- **Drawn by Q2 Pod:** Weather is now a card app, as Mixtape is: `q2app` without `game=1`, a `run` launcher, and one program, `q2-weather`, that answers Q2 Pod's pages (Q2SPOT/1). Q2 Pod draws every page itself, so the fonts, rows, selection, title bar, scrolling, click wheel, touch and theme (Minimal, Classic, Modern, every accent, Light Mode) are the firmware's own rather than a copy of them. Its row stays in Library → Apps.
- **Pages:** the first page lists My Location (or Home City) and the Saved City with their temperature and condition, then Settings; a place's page shows now, today's high and low and humidity, then seven days with condition, high / low and chance of rain, then when it was updated. Settings holds Temperature, Home City, Saved City, Remove Saved City and Update Now.
- **City search** uses Q2 Pod's own search box: the title bar's magnifying glass finds a Saved City, and Settings' Home City and Saved City rows open a **City name:** box. The built-in QWERTY keyboard is gone.
- **Condition pictures:** each place and day row has a 64 px condition picture beside it, shown as Q2 Pod shows an album's cover (original drawings, drawn at build time by `tools/draw_icons.py`). They're written whole and renamed into place, and only when the condition changes.
- Overcast (code 3) reads **Overcast** and code 1 **Mostly clear**, rather than both as Partly cloudy.
- Temperatures show as `22°`. The units choice is kept in `.weather/units`, as before.
- A first update that finds no network (the Apps row opens before Smart Wi-Fi's link) is tried again up to three times, 10 s apart.

### Removed
- The full-screen dashboard (`src/dashboard*.c`, `src/q2pod_scheme.c`, `src/theme.c`, `src/lang.c`), its built-in fonts and the `lang/` packs, and `q2-weather-service` as a separate file.

### Checks
- `tests/test_pages.py` runs the host build on a cached forecast and checks the rows of every page; `make check` runs it.
- On-device verification is still to do.

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
