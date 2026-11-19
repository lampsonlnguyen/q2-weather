# Q2 Weather

A weather application in development for the Shanling Q2 running [Q2 Pod](https://github.com/DiamondBond/q2-pod).

## Design

- Two location slots: **Current Location** (automatically detected using public-IP geolocation on each launch) and **Saved Location** (one user-selected city). Slot 0 is automatically managed; slot 1 is user-editable.
- [Open-Meteo](https://open-meteo.com/en/docs) is the planned weather source. **No API key required** for non-commercial use.
- [ipwho.is](https://ipwho.is/) is the planned public-IP geolocation service over HTTPS, requiring no key on its free public endpoint.
- On **every app launch**, call `weather_on_open`: detect the current location and refresh both slots. Keep cached readings visible if offline and label them as cached.
- Weather fields: Celsius temperature, humidity, WMO weather condition code, last-updated timestamp, plus **7 days of high/low temperatures, weather descriptions (WMO codes), and precipitation probability** for both locations.
- Forecast data is fetched on every app launch alongside current conditions. The location-specific daily date comes from Open-Meteo's timezone-aware response.
- When refreshing: read Wi-Fi radio state; if off, enable and connect to a saved network; query IP location (if requested); refresh weather for both locations; **disable Wi-Fi only if the app enabled it**. A failed request should still trigger restoration.
- If Wi-Fi was already enabled, leave it enabled on success or failure.
- Persist locations and last successful readings on the SD card, and show when cached weather is stale.

## Development status

**Current implementation is a portable, tested weather-state and Wi-Fi ownership core. It is not yet a runnable Q2 card application.** The device integration still needs firmware-specific radio controls, HTTPS fetching, JSON parsing, a UI, SD-card storage hooks and a MIPS cross-build. Do not install this repository on a Q2 yet.

The launch-refresh entrypoint and seven-day forecast data model are implemented in the portable core, but neither is yet wired to a Q2 executable. The core uses injected callbacks rather than guessing the Q2 firmware's Wi-Fi command, network executable, or TLS support. This prevents an implementation from silently changing saved Wi-Fi settings or sending weather requests over insecure HTTP. API endpoints intended for the adapter:

```text
https://ipwho.is/
https://api.open-meteo.com/v1/forecast?latitude=33.77&longitude=-117.94&current=temperature_2m,relative_humidity_2m,weather_code&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code&forecast_days=7&timezone=auto
```

IP location reflects the internet exit point and may be wrong when using a VPN or carrier network. Manual location selection should always remain available.

## Tests

Run `make check` on Linux/WSL/macOS with a C compiler. Tests cover two locations, persistence, current-location lookup, preserving initially enabled Wi-Fi, and restoring initially disabled Wi-Fi when networking, geolocation or weather retrieval fails.

## Roadmap

1. Verify actual Wi-Fi API and TLS client on the Shanling Q2 firmware.
2. Implement Q2 device transport and network-backed adapters for ipwho.is and Open-Meteo.
3. Add on-device city search and manual coordinate entry, location chooser, refresh action, and weather display using Q2 input conventions.
4. Package under `.weather/` with an appropriate Q2 Pod SD-card app manifest and GitHub Actions workflow.
