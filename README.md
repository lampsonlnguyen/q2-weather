# Q2 Weather

An SD-card Library app for the Shanling Q2 running [DiamondBond's Q2 Pod](https://github.com/DiamondBond/q2-pod), using its native card-app browser. Current observations and seven days of weather for two locations.

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
3. Insert the card in the Q2 running a recent Q2 Pod release with Library card-app support (Q2 Pod **1.8.7 or later**).
4. Open **Library → Weather**. The first screen starts a background refresh for both locations.
5. Choose **Saved Location → Change saved city**, type the city, then select the search result. Return to Weather to see the new forecast.

The first launch requires the Q2 to be connected to Wi-Fi. Once data is cached, the pages remain readable without an internet connection.

### Wi-Fi behavior and current limitation

Weather observes Q2 Pod's Wi-Fi setting and uses the firmware's existing **Smart Wi-Fi** behavior. When Wi-Fi is enabled, Q2 Pod holds it awake while the Weather card service is running. Weather **does not turn off an already enabled radio**. The app's networking layer includes the requested Wi-Fi ownership callbacks and error cleanup.

**If the user's Wi-Fi switch is manually OFF**, this standalone card app currently **cannot temporarily override it**. Q2 Pod's internal `wifi_switch` function is not available as a supported external-app API. Weather therefore leaves that preference unchanged, shows a Wi-Fi-off message, and uses its cached forecast. Implementing temporary Wi-Fi access with the switch OFF needs a small coordinated Q2 Pod firmware change; scripting direct radio/device settings would risk corrupting the user's network state.

## Build and tests

On Ubuntu or WSL, install `clang clang-format lld gcc make libcurl4-openssl-dev squashfs-tools unzip zip curl xz-utils binutils`.

```sh
make check
make format-check
sh tools/sysroot.sh
make
```

`tools/sysroot.sh` obtains the Q2-compatible glibc runtime from a Q2 Pod release and known-version Debian Buster mipsel development headers. The output is `dist/q2-weather.zip`, with a native **MIPS32 little-endian** executable.

GitHub Actions runs clang-format automatically, host regression tests, the MIPS cross-build, ZIP validation and artifact upload on pushes to `main`. A `v*` tag publishes a **prerelease** with the device test ZIP. A built ZIP is a test candidate, not a claim of physical-device verification.

## Architecture

- `src/main.c`: Q2SPOT/1 Unix socket server, Library menu pages, on-device location selection, asynchronous refresh and SD-card persistence.
- `src/http.c`: HTTPS with Q2's stock libcurl, ipwho.is geolocation, Open-Meteo geocoding and weather data.
- `src/weather.c`: portable two-location application state, Wi-Fi ownership callbacks and versioned forecast cache serialization.
- `src/crt-q2.c`: Q2 glibc startup shim, from [DiamondBond/q2-mixtape](https://github.com/DiamondBond/q2-mixtape), GPL-2.0-or-later.
- `q2app`, `run`: native Library card-app integration.
- `tools/sysroot.sh`: cross-compilation dependencies from Q2 Pod and Debian, based on the Q2 Rockbox sysroot method.

## Acknowledgments

Thanks to DiamondBond for Q2 Pod and the native card-app framework, and the Mixtape, Buddy and Groovebox implementations that established a reliable Q2 development pattern.

This project incorporates the GPL-2.0-or-later Q2 startup shim by DiamondBond; the other original weather files are marked MIT.
