# Q2 Weather

An SD-card Library app for the Shanling Q2 running [DiamondBond's Q2 Pod](https://github.com/DiamondBond/q2-pod), using its native card-app browser. Current observations and seven days of weather for two locations.

## Full graphical dashboard (v0.3.0)

Open **Library → Weather Dashboard** to see the new lightweight full-screen graphical view. It draws a dark themed dashboard with current and saved city cards, large pixel weather art, and seven selectable forecast days. Turn the Q2 wheel to select a day; press Centre for its high/low/rain detail and Return to go back or exit. Tap a location card to change the active city or tap a day to open its forecast. Music playback controls remain with Q2 Pod.

The original **Library → Weather** text/art interface remains available for city search, manual updates, unit selection and troubleshooting. Both applications share the same `.weather/state` file. The graphical view starts the background weather service when necessary and reads its cache once per second; **it only redraws the framebuffer when the cache or input changes**, not on every poll. The graphics and bitmap font are compiled from build-time image generation (no SDL, game sound, animations, runtime image decoding or external font files).

The ZIP installs both `.weather` and `.weather-view` at the microSD root. Do not delete the `.weather/state` file during an upgrade. The screen and input path follow Q2 Pod's native full-screen game handoff, but the dashboard is explicitly placed in Library (`game=1`, `row=library`). Physical Q2 validation is required before treating the graphics interface as fully verified.

## Weather artwork (v0.2.0)

The Library app displays original pixel-weather artwork beside both location rows and each of the seven forecast days. This uses **Q2 Pod's native JPEG art-row feature**, not a custom framebuffer or game UI. It preserves Q2 Pod's theme, wheel scroll, Return button, city-entry keyboard, and background music. Q2 Pod's art rows require a firmware version supporting the `art` capability; older versions continue to show the text-only interface.

The icons are small original sprites drawn at build time by `tools/draw_weather_icons.py` (Pillow). They are scaled with nearest-neighbor to 96 × 96 JPEGs, stored under `.weather/icons/`, and copied into Q2 Pod's `/tmp/q2-weather.art/` when each row is shown. There is no PNG decoder, framebuffer polling, continuous animation, or image conversion on the device. A 52 × 52 and a 40 × 40 launcher icon are also included.

This is a lightweight **native Library art-row implementation**, rather than the full-screen cover-flow dashboard depicted in the earlier conceptual mockup. The firmware retains control of row layout and navigation.

## Features

- **Current Location:** approximate city detected from the Q2's public IP via [ipwho.is](https://ipwho.is/).
- **Saved Location:** one city chosen with Q2 Pod's on-device text-entry keyboard, resolved through [Open-Meteo Geocoding](https://open-meteo.com/en/docs/geocoding-api).
- **Current conditions:** temperature and relative humidity, plus a description based on the WMO code.
- **Seven-day forecast:** daily high, low, rain probability and weather code for both cities.
- **Automatically updates every time you open Weather.** The Library page is responsive while the network worker runs.
- **Offline cache:** the last known weather and seven-day forecast are stored in `.weather/state`. A failed refresh retains the cached data.
- Switch between Celsius and Fahrenheit, and refresh manually.
- No API keys required for non-commercial use. Weather data comes from [Open-Meteo](https://open-meteo.com/en/docs).

Public-IP geolocation is approximate and can show a nearby or different city, especially when using VPNs.

## Installation

1. Download **q2-weather.zip** from a successful [GitHub Actions](https://github.com/lampsonlnguyen/q2-weather/actions) build artifact or a tagged [release](https://github.com/lampsonlnguyen/q2-weather/releases).
2. Extract the **inner ZIP** at the root of your microSD card. It must contain:
   - `.weather/q2app`
   - `.weather/run`
   - `.weather/q2-weather`
   - `.weather-view/q2app`
   - `.weather-view/q2-weather-view`
3. Insert the card in the Q2 running a recent Q2 Pod release with Library card-app support (Q2 Pod **1.8.7 or later**).
4. Open **Library → Weather**. The first screen starts a background refresh for both locations.
5. Choose **Saved Location → Change saved city**, type the city, then select the search result. Return to Weather to see the new forecast.

The first launch requires the Q2 to be connected to Wi-Fi. Once data is cached, the pages remain readable without an internet connection.

### Wi-Fi behavior and current limitation

Weather observes Q2 Pod's Wi-Fi setting and uses the firmware's existing **Smart Wi-Fi** behavior. When Wi-Fi is enabled, Q2 Pod holds it awake while the Weather card service is running. Weather **does not turn off an already enabled radio**. The app's networking layer includes the requested Wi-Fi ownership callbacks and error cleanup.

**If the user's Wi-Fi switch is manually OFF**, this standalone card app currently **cannot temporarily override it**. Q2 Pod's internal `wifi_switch` function is not available as a supported external-app API. Weather therefore leaves that preference unchanged, shows a Wi-Fi-off message, and uses its cached forecast. Implementing temporary Wi-Fi access with the switch OFF needs a small coordinated Q2 Pod firmware change; scripting direct radio/device settings would risk corrupting the user's network state.

## Build and tests

On Ubuntu or WSL, install `clang clang-format lld gcc make libcurl4-openssl-dev squashfs-tools unzip zip curl xz-utils binutils python3-pil`.

```sh
make check
make format-check
sh tools/sysroot.sh
make
```

`tools/sysroot.sh` obtains the Q2-compatible glibc runtime from a Q2 Pod release and known-version Debian Buster mipsel development headers. The output is `dist/q2-weather.zip`, with a native **MIPS32 little-endian** executable.

GitHub Actions runs clang-format automatically, host regression tests, the MIPS cross-build, ZIP validation and artifact upload on pushes to `main`. A `v*` tag publishes a **prerelease** with the device test ZIP. A built ZIP is a test candidate, not a claim of physical-device verification.

## Architecture

- `src/main.c`: Q2SPOT/1 Unix socket server, Library menu pages, on-device location selection, asynchronous refresh, art-row hooks and SD-card persistence.
- `src/art.c`: lightweight weather-code mapping and Q2 Pod native art-row JPEG integration.
- `tools/draw_weather_icons.py`: generates original 96-pixel weather icons, 24-pixel C sprite atlas, and bitmap font at build time. No on-device Python or PNG decoder.
- `src/dashboard.c`, `src/dashboard_render.c`, `src/dashboard_q2.c`: event-driven full-screen view, sprite renderer and native framebuffer/touch/wheel platform.
- `src/http.c`: HTTPS with Q2's stock libcurl, ipwho.is geolocation, Open-Meteo geocoding and weather data.
- `src/weather.c`: portable two-location application state, Wi-Fi ownership callbacks and versioned forecast cache serialization.
- `src/crt-q2.c`: Q2 glibc startup shim, from [DiamondBond/q2-mixtape](https://github.com/DiamondBond/q2-mixtape), GPL-2.0-or-later.
- `q2app`, `run`: native Library card-app integration.
- `tools/sysroot.sh`: cross-compilation dependencies from Q2 Pod and Debian, based on the Q2 Rockbox sysroot method.

## Acknowledgments

Thanks to DiamondBond for Q2 Pod and the native card-app framework, and the Mixtape, Buddy and Groovebox implementations that established a reliable Q2 development pattern.

This project incorporates the GPL-2.0-or-later Q2 startup shim by DiamondBond; the other original weather files are marked MIT.
