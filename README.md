# Q2 Weather

A lightweight, full-screen weather application for the Shanling Q2 running [Q2 Pod](https://github.com/DiamondBond/q2-pod). **One app under Library → Weather.**

## Features

- One Weather Library app that opens to a **location list**. Select Current Location or Saved Location to expand into that location's graphical seven-day forecast dashboard.
- Current location detected approximately from the Q2 public IP using [ipwho.is](https://ipwho.is/).
- One saved city using Q2 Pod's **native T9 keyboard** (requires the [Q2 Pod Weather integration firmware](https://github.com/lampsonlnguyen/q2-pod/blob/main/patch/addon.c)) and [Open-Meteo geocoding](https://open-meteo.com/en/docs/geocoding-api).
- Weather conditions, Celsius/Fahrenheit, humidity, daily high/low and precipitation chance.
- Offline cache; background refresh on opening the dashboard and explicit refresh in Settings.
- No API key, SDL, animation loop, runtime JPEG/PNG decoding, custom keyboard, or sound engine.
- Q2 Pod keeps controlling music; the Weather application does not take audio ownership.

## Controls

| Screen | Wheel | Centre | Return |
| --- | --- | --- | --- |
| Locations | Select current or saved location | Open selected location's dashboard | Exit Weather |\n| Forecast dashboard | Select forecast day | Open day details | Locations |
| Day detail | Select day | Return to forecast dashboard | Forecast dashboard |
| Settings | Select setting | Activate setting | Dashboard |
| City entry | Q2 Pod native T9 keyboard handles wheel input | Use its Done key to search | Cancel / Settings |

**Touch:** Tap **SETTINGS** on the initial Locations screen, tap Current Location or Saved Location to open its forecast dashboard, then tap a forecast day for details. An unconfigured saved location opens city entry automatically. The native Q2 Pod keyboard opens above the graphical screen for city entry; press its Done button to search. City lookup runs off the graphics thread. If the firmware lacks the required native-keyboard bridge, Weather displays an update notice instead of drawing a custom keyboard.

**Settings:** Change saved city; toggle Celsius/Fahrenheit; refresh both locations; clear saved city. Weather follows Q2 Pod's Minimal, Classic and Modern themes, their separate accent choices, and Light Mode (including automatic day/night). The screen has centered title and rounded-corner-safe margins.

## Install or upgrade

Download the latest `q2-weather.zip` from [GitHub Releases](https://github.com/lampsonlnguyen/q2-weather/releases) and extract it **to the root of your Q2 microSD card**. The single application has this structure:

```text
.weather/
├── q2app                  # name=Weather, game=1, row=library
├── q2-weather             # graphical executable
├── q2-weather-service     # internal weather/HTTPS helper, not a second app
├── lang/0.lang             # editable English UI labels; future numeric language packs
├── icon.png
├── state                  # created on Q2; retain when upgrading
└── units                  # created on Q2
```

**Important if upgrading from v0.3.x:** Delete the old `.weather-view` folder from the SD card. Otherwise Q2 Pod may still show the previously installed **Weather Dashboard** as a second app. Do **not** delete `.weather/state`; your saved location and offline forecast live there.

Weather is displayed under **Library → Weather**, rather than **Library → Games**. It uses Q2 Pod's supported card-game fullscreen display handoff, but remains in Library with `game=1` and `row=library`.

## Firmware integration required for native keyboard and Smart Wi-Fi

The full-screen Weather app is a Q2 Pod `game=1` card program. Stock firmware
does not expose its native T9 keyboard or count full-screen card programs
as Smart Wi-Fi network consumers. To provide the requested behavior, the
[Q2 Pod fork's `patch/addon.c`](https://github.com/lampsonlnguyen/q2-pod/blob/main/patch/addon.c)
includes a Weather-specific native keyboard handoff and Smart Wi-Fi integration.

- When entering a saved city, Weather sends a request marker and Q2 Pod opens
  its own T9 input method. The completed UTF-8 city is returned to the Weather
  app's input socket. No on-screen keyboard is implemented in Weather.
- The firmware counts **Weather only** as a Smart Wi-Fi network consumer while
  it is open; other offline games do not wake Wi-Fi. The normal firmware's
  idle timer handles returning Wi-Fi to sleep after Weather closes.
- Smart Wi-Fi still requires its Power setting to be enabled and the user's
  own Wi-Fi setting to be On. It does not override an intentional Wi-Fi Off.
- On firmware without this integration the native city keyboard cannot open;
  Weather displays a firmware-update notice. **Do not install a custom Q2 Pod
  firmware build without verifying it on your device.** These firmware
  changes are not yet hardware-verified.

The fork's [build instructions](https://github.com/lampsonlnguyen/q2-pod/blob/main/docs/building.md)
describe how to produce a development `update.tar`. A Weather ZIP does not
install or modify Q2 Pod firmware.

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

`src/dashboard_q2.c` handles framebuffer painting only on input/data changes, Q2 input datagrams, and a slow 1-second cache check. `src/dashboard_render.c` draws theme-adaptive, high-contrast screens and user-replaceable PNG sprites compiled by `tools/pack_sprites.py`. It uses a proportionally spaced, anti-aliased sans-serif typeface baked into the app at build time. `src/dashboard.c` handles wheel, touch, keyboard and page navigation. `src/main.c`, `src/http.c` and `src/weather.c` provide the internal HTTPS/weather helper and cache.

There is **one registered card app** and two executable processes only while the app is open: the graphical UI and its internal HTTP/weather worker. The worker is not a second Library entry. No continuous animation is used. Measured power usage and physical device behavior still require testing on Q2 hardware.

The cache is stored on the SD card at `.weather/state`, and network diagnostics at `.weather/weather.log`. IP-based location can be approximate or affected by a VPN.

## License and credits

Built for Q2 Pod with thanks to DiamondBond and the Q2 community. The small sprite-rendering approach follows the technical design of [q2-diamond-crush](https://github.com/lampsonlnguyen/q2-diamond-crush). Weather data comes from Open-Meteo.

### Weather icon attribution

The color weather icons are adapted from [Tomorrow.io Weather Code Icons](https://github.com/Tomorrow-IO-API/tomorrow-weather-codes), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). **Powered by Tomorrow.io** ([weather API](https://www.tomorrow.io/weather-api/)) — artwork only; forecasts continue to come from Open-Meteo. Attributions and the exact original asset mapping are documented in [assets/sprites/ATTRIBUTION.md](assets/sprites/ATTRIBUTION.md). The `wind.png` and `humidity.png` icons are original Q2 Weather artwork. Replace any PNG under `assets/sprites/` to customize your own build; the build resizes all icons automatically.
