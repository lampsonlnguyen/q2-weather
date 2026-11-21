# Q2 Weather

A lightweight, full-screen weather application for the Shanling Q2 running [Q2 Pod](https://github.com/DiamondBond/q2-pod). **One app under Library → Weather.**

**Latest release: v1.3.0** — independent weather and location fallbacks. See [CHANGELOG.md](CHANGELOG.md) for the complete release notes.

## Primary and fallback services

| Purpose | Primary | Automatic fallback | When both fail |
| --- | --- | --- | --- |
| Weather and 7-day forecast | [Open-Meteo Forecast](https://open-meteo.com/en/docs) | [MET Norway Locationforecast 2.0](https://api.met.no/weatherapi/locationforecast/2.0/documentation) | Retain previously cached weather; no fresh forecast until a provider is reachable |
| Approximate Current Location by public IP | [ipwho.is](https://ipwho.is/) | [ipapi.co](https://ipapi.co/) | Set Home City manually from the Current Location entry or Settings |
| City search for manual selection | [Open-Meteo Geocoding](https://open-meteo.com/en/docs/geocoding-api) | [OpenStreetMap Nominatim](https://nominatim.org/) | Keep any previously selected location; retry search when connectivity returns |
| Saved Location weather | Stored latitude/longitude → Open-Meteo | Same stored coordinates → MET Norway | Keep the Saved Location and cached forecast; **never requires Current Location detection** |

### How location fallback works

- **Current Location:** Automatic location is approximate (based on the public IP address, not GPS). If both IP-location services fail, select an unconfigured Current Location, or open **Settings → Set Home City**, and pick a city using the built-in QWERTY keyboard.
- **Manual Home City:** The chosen city's coordinates persist across restarts and prevent automatic IP detection from replacing it. To return to automatic detection, remove `.weather/manual-home` from the microSD card.
- **Saved Location:** Uses its stored coordinates directly. Its weather request runs **before** IP-based Current Location detection, so an IP service outage does not block Saved Location.
- **Forecast fallback:** Open-Meteo is tried first. When it fails or returns incomplete daily data, the app tries MET Norway. MET Norway's UTC time series is grouped into seven daily forecasts, so its daily high/low values and day boundaries may differ from Open-Meteo. Precipitation percentages that the fallback does not provide are displayed as unavailable rather than guessed.
- **Offline:** A manual city choice still requires the city-search API to be reachable (Open-Meteo or Nominatim). Selecting a city cannot restore weather forecasts when *both* forecast providers are unreachable. Existing cached forecasts remain available.
- **Provider attribution:** Fallback forecast data © Norwegian Meteorological Institute (MET Norway), CC BY 4.0. Place-search fallback data © OpenStreetMap contributors, ODbL.

## Features

- One Weather Library app that opens to a **location list**. Select Current Location or Saved Location to expand into that location's seven-day forecast list.
- Current Location supports two public-IP providers and persistent manual Home City selection.
- One Saved Location with direct coordinate-based forecasts, plus a separate manual Home City. The **built-in QWERTY keyboard** supports click-wheel and touchscreen city search with a geocoding fallback and up to eight matches.
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

**Touch:** Tap a location row on the initial Locations screen to open its forecast dashboard, then tap a forecast day for details; tap the Settings row for settings. An unconfigured Current or Saved Location opens city entry automatically. The built-in QWERTY keyboard supports an **ABC/123** toggle for digits and punctuation, **SPACE**, **CLEAR** (erase the entire city entry), **DEL** (delete one character), and **SEARCH**, all selectable with the click-wheel or touchscreen. Settings is also a third highlighted click-wheel choice on the initial locations screen. City lookup runs off the graphics thread; a firmware keyboard patch is not required. After searching, a result picker displays three matches at a time with safe spacing for the Q2's rounded screen. Scroll to a matching state/province and country, then press Center or tap to save it. The selection is checked against the returned coordinates before writing the location cache. A saved location includes the region and country to distinguish similarly named cities.

**Settings:** Change saved city; toggle Celsius/Fahrenheit; refresh both locations; clear saved city; set Home City manually. Weather follows Q2 Pod's Minimal, Classic and Modern themes, their separate accent choices, and Light Mode (including automatic day/night). The screen has centered title and rounded-corner-safe margins.

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
├── units                  # created on Q2
└── manual-home            # optional, created after manual Home City selection
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

The launcher artwork is the editable [assets/icon.png](assets/icon.png): a vivid yellow-gold sun and white cloud on blue. The build resizes it to the Q2's 52-pixel app icon automatically. It is the app's only image; weather conditions are text only, with no condition icons anywhere.

`src/dashboard_q2.c` handles framebuffer painting only on input/data changes, Q2 input datagrams, and a slow 1-second cache check. `src/dashboard_render.c` draws q2-pod list screens — 26 px status bar, 32 px rows, the active theme's selection painter (Minimal's card, Classic's gloss stops, Modern's accent bar) — from the scheme data ported in `src/q2pod_scheme.c` from q2-pod's own theme files; conditions are text only. It uses a proportionally spaced, anti-aliased sans-serif typeface baked into the app at build time by `tools/pack_fonts.py`. `src/dashboard.c` handles wheel, touch, keyboard and page navigation. `src/main.c`, `src/http.c` and `src/weather.c` provide the internal HTTPS/weather helper and cache.

There is **one registered card app** and two executable processes only while the app is open: the graphical UI and its internal HTTP/weather worker. The worker is not a second Library entry. No continuous animation is used. Measured power usage and physical device behavior still require testing on Q2 hardware.

The cache is stored on the SD card at `.weather/state`, and network diagnostics at `.weather/weather.log`. IP-based location can be approximate or affected by a VPN.

## License and credits

Built for Q2 Pod with thanks to DiamondBond and the Q2 community. The app inherits its look from q2-pod's own themes (Minimal, Classic, Modern) rather than drawing its own. Weather forecasts use Open-Meteo with MET Norway as a fallback. IP-based location uses ipwho.is with ipapi.co as fallback; city search uses Open-Meteo geocoding with OpenStreetMap Nominatim as fallback.

### Weather art

Conditions are text only — the app ships no weather icons. q2-pod's native art rows still work if JPEGs are placed in `.weather/icons/` on the SD card (`src/art.c` publishes them keyed the same way q2-pod does).
