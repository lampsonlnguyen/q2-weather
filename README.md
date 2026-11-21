# Q2 Weather

## Independent weather and location fallbacks

- **Forecast:** Open-Meteo is primary. If the forecast request fails, the Q2 tries the independently hosted [MET Norway Locationforecast 2.0](https://api.met.no/weatherapi/locationforecast/2.0/documentation). Its hourly UTC readings are aggregated into seven daily forecasts. MET Norway does not supply the same precipitation probability fields, so absent probabilities display as unavailable.
- **Current/Home Location:** Approximate public-IP detection tries `ipwho.is` and then `ipapi.co`. If both fail, open **Current Location** or **Settings → Set Home City** to search and select a city manually.
- **Manual city search:** Open-Meteo geocoding is primary; OpenStreetMap Nominatim is the independent backup. Choosing a city saves its coordinates for later forecast requests. A manually set Home stays set between restarts; to restore automatic IP location detection, delete `.weather/manual-home` from the SD card.
- **Saved Location:** Uses saved latitude/longitude directly and fetches its forecast *before* any public-IP location detection, including when both IP providers are down. It never needs the current-location services.
- **Offline:** Previously cached weather remains visible. If both forecast providers are unavailable, changing the location cannot produce new weather until at least one forecast provider recovers.
- **Attribution:** Backup forecast data © Norwegian Meteorological Institute (MET Norway), CC BY 4.0. Backup place-search data © OpenStreetMap contributors, ODbL.


A lightweight, full-screen weather application for the Shanling Q2 running [Q2 Pod](https://github.com/DiamondBond/q2-pod). **One app under Library → Weather.**

## Features

- One Weather Library app that opens to a **location list**. Select Current Location or Saved Location to expand into that location's graphical seven-day forecast dashboard.
- Current location detected approximately from the Q2 public IP using [ipwho.is](https://ipwho.is/).
- One saved city using Weather's **built-in QWERTY keyboard**, operated by the click-wheel or touchscreen and [Open-Meteo geocoding](https://open-meteo.com/en/docs/geocoding-api). Searches return up to eight **city, state/province, country** matches so you select the exact destination.
- Weather conditions, Celsius/Fahrenheit, humidity, daily high/low and precipitation chance.
- Offline cache; background refresh on opening the dashboard and explicit refresh in Settings.
- No API key, SDL, animation loop, runtime JPEG/PNG decoding, or sound engine.
- Q2 Pod keeps controlling music; the Weather application does not take audio ownership.

## Controls

| Screen | Wheel | Centre | Return |
| --- | --- | --- | --- |
| Locations | Select Current, Saved, or Settings | Open highlighted location or Settings | Exit Weather |\n| Forecast dashboard | Select forecast day | Open day details | Locations |
| Day detail | Select day | Return to forecast dashboard | Forecast dashboard |
| Settings | Select setting | Activate setting | Dashboard |
| City entry | Select QWERTY key | Type / Space / Clear / Backspace / Search | Cancel / Settings |
| City results | Scroll matching locations | Save highlighted city and coordinates | Return to QWERTY keyboard |

**Touch:** Tap **SETTINGS** on the initial Locations screen, tap Current Location or Saved Location to open its forecast dashboard, then tap a forecast day for details. An unconfigured saved location opens city entry automatically. The built-in QWERTY keyboard supports an **ABC/123** toggle for digits and punctuation, **SPACE**, **CLEAR** (erase the entire city entry), a drawn **backspace arrow** (delete one character), and **SEARCH**, all selectable with the click-wheel or touchscreen. Settings is also a third highlighted click-wheel choice on the initial locations screen. City lookup runs off the graphics thread; a firmware keyboard patch is not required. After searching, a result picker displays three matches at a time with safe spacing for the Q2's rounded screen. Scroll to a matching state/province and country, then press Center or tap to save it. The selection is checked against the returned coordinates before writing the location cache. A saved location includes the region and country to distinguish similarly named cities.

**Settings:** Change saved city; toggle Celsius/Fahrenheit; refresh both locations; clear saved city. Weather follows Q2 Pod's Minimal, Classic and Modern themes, their separate accent choices, and Light Mode (including automatic day/night). The screen has centered title and rounded-corner-safe margins.

## Install or upgrade

Download the latest `q2-weather.zip` from [GitHub Releases](https://github.com/lampsonlnguyen/q2-weather/releases) and extract it **to the root of your Q2 microSD card**. The single application has this structure:

```text
.weather/
├── q2app                  # name=Weather, game=1, row=library
├── q2-weather             # graphical executable
├── q2-weather-service     # internal weather/HTTPS helper, not a second app
├── lang/0.lang             # editable English UI labels; future numeric language packs
├── icon.png                # yellow-sun launcher (from assets/icon.png)
├── state                  # created on Q2; retain when upgrading
└── units                  # created on Q2
```

**Important if upgrading from v0.3.x:** Delete the old `.weather-view` folder from the SD card. Otherwise Q2 Pod may still show the previously installed **Weather Dashboard** as a second app. Do **not** delete `.weather/state`; your saved location and offline forecast live there.

Weather is displayed under **Library → Weather**, rather than **Library → Games**. It uses Q2 Pod's supported card-game fullscreen display handoff, but remains in Library with `game=1` and `row=library`.

## Keyboard and Smart Wi-Fi

Weather's built-in QWERTY keyboard works with existing Q2 Pod firmware.
The firmware-native T9 handoff was experimental and is **not required** by this
release. This preserves reliable city search without patching the firmware.
Click-wheel direction follows normal Q2 menu navigation (clockwise moves forward).

The [Q2 Pod fork](https://github.com/lampsonlnguyen/q2-pod/blob/main/patch/addon.c)
has a separate experimental Smart Wi-Fi integration that counts the full-screen
Weather app as a network user while it is open. That still requires rebuilding
and installing Q2 Pod firmware, and has not been validated on physical hardware.
It respects the manual Wi-Fi Off setting. Weather itself never directly toggles
the system radio.

## System language and language packs

Weather reads Q2 Pod's `SYSSET/LANGUAGE` numeric setting if the firmware
exposes it and loads the matching `.weather/lang/<id>.lang` file. The
included `assets/lang/0.lang` is the editable English pack. Missing strings
and missing packs fall back to English. A second language can be added later
without changing the UI logic, but translated labels using non-ASCII
characters also need expanded glyph coverage. Current Q2 Pod hides its stock
Language picker pending firmware-wide translation; therefore English remains
the default on current builds.

## Build on Ubuntu / WSL

```sh
sudo apt update
sudo apt install -y clang clang-format lld make gcc libcurl4-openssl-dev \
  squashfs-tools unzip zip curl xz-utils binutils python3-pil
git pull origin main
sh tools/sysroot.sh build/sysroot build/q2-rootfs.squashfs
make clean
make check
make
```

### If a local build reports missing libcurl.so.4

Some older cached sysroots (for example `/home/lam/.cache/q2/sysroot`) have the
glibc headers but do not include Q2's `libcurl.so.4`. The Makefile now repairs
this automatically: it first checks `$(SYSROOT)/usr/lib/libcurl.so.4`, then
extracts the **Q2 MIPS** library from a cached `q2-rootfs.squashfs`.
This does not replace or rebuild your existing sysroot.

If no Q2 rootfs is available, initialize or repair it once:

```sh
sh tools/sysroot.sh /home/lam/.cache/q2/sysroot build/q2-rootfs.squashfs
make
```

Use your actual `SYSROOT` path. Do not copy the host Ubuntu x86-64 libcurl
into the MIPS sysroot.

If `build/sysroot` already exists, skip the sysroot command. The Q2 root filesystem is generated from the locally supplied stock firmware or downloaded by the sysroot script. The resulting ZIP is `dist/q2-weather.zip`. **Do not commit stock firmware or the sysroot.**

## Architecture and power

The launcher artwork is the editable [assets/icon.png](assets/icon.png): a vivid yellow-gold sun and white cloud on blue, distinct from individual forecast sprites. The build resizes it to the Q2's 52-pixel app icon automatically.

`src/dashboard_q2.c` handles framebuffer painting only on input/data changes, Q2 input datagrams, and a slow 1-second cache check. `src/dashboard_render.c` draws theme-adaptive, high-contrast screens and user-replaceable PNG sprites compiled by `tools/pack_sprites.py`. It uses a proportionally spaced, anti-aliased sans-serif typeface baked into the app at build time. `src/dashboard.c` handles wheel, touch, keyboard and page navigation. `src/main.c`, `src/http.c` and `src/weather.c` provide the internal HTTPS/weather helper and cache.

There is **one registered card app** and two executable processes only while the app is open: the graphical UI and its internal HTTP/weather worker. The worker is not a second Library entry. No continuous animation is used. Measured power usage and physical device behavior still require testing on Q2 hardware.

The cache is stored on the SD card at `.weather/state`, and network diagnostics at `.weather/weather.log`. IP-based location can be approximate or affected by a VPN.

## License and credits

Built for Q2 Pod with thanks to DiamondBond and the Q2 community. The small sprite-rendering approach follows the technical design of [q2-diamond-crush](https://github.com/lampsonlnguyen/q2-diamond-crush). Weather data comes from Open-Meteo.

### Weather icon attribution

The color weather icons are adapted from [Tomorrow.io Weather Code Icons](https://github.com/Tomorrow-IO-API/tomorrow-weather-codes), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). **Powered by Tomorrow.io** ([weather API](https://www.tomorrow.io/weather-api/)) — artwork only; forecasts continue to come from Open-Meteo. Attributions and the exact original asset mapping are documented in [assets/sprites/ATTRIBUTION.md](assets/sprites/ATTRIBUTION.md). The `wind.png` and `humidity.png` icons are original Q2 Weather artwork. Replace any PNG under `assets/sprites/` to customize your own build; the build resizes all icons automatically.
