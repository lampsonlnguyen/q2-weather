# Changelog

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
