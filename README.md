# Q2 Weather

A lightweight, full-screen weather application for the Shanling Q2 running [Q2 Pod](https://github.com/DiamondBond/q2-pod). **One app under Library → Weather.**

## Features

- One Weather Library app that opens to a **location list**. Select Current Location or Saved Location to expand into that location's graphical seven-day forecast dashboard.
- Current location detected approximately from the Q2 public IP using [ipwho.is](https://ipwho.is/).
- One saved city using an in-app wheel/touch keyboard and [Open-Meteo geocoding](https://open-meteo.com/en/docs/geocoding-api).
- Weather conditions, Celsius/Fahrenheit, humidity, daily high/low and precipitation chance.
- Offline cache; background refresh on opening the dashboard and explicit refresh in Settings.
- No API key, SDL, animation loop, runtime JPEG/PNG decoding, or custom sound engine.
- Q2 Pod keeps controlling music; the Weather application does not take audio ownership.

## Controls

| Screen | Wheel | Centre | Return |
| --- | --- | --- | --- |
| Locations | Select current or saved location | Open selected location's dashboard | Exit Weather |\n| Forecast dashboard | Select forecast day | Open day details | Locations |
| Day detail | Select day | Return to forecast dashboard | Forecast dashboard |
| Settings | Select setting | Activate setting | Dashboard |
| City entry | Select character | Type selected character | Settings |

**Touch:** Tap **SETTINGS** on the initial Locations screen, tap Current Location or Saved Location to open its forecast dashboard, then tap a forecast day for details. An unconfigured saved location opens city entry automatically. In the city-entry keyboard, use `<` to delete a character, `_` for a space and `+` to search and save. City lookup runs off the graphics thread.

**Settings:** Change saved city; toggle Celsius/Fahrenheit; refresh both locations; clear saved city.

## Install or upgrade

Download the latest `q2-weather.zip` from [GitHub Releases](https://github.com/lampsonlnguyen/q2-weather/releases) and extract it **to the root of your Q2 microSD card**. The single application has this structure:

```text
.weather/
├── q2app                  # name=Weather, game=1, row=library
├── q2-weather             # graphical executable
├── q2-weather-service     # internal weather/HTTPS helper, not a second app
├── icon.png
├── state                  # created on Q2; retain when upgrading
└── units                  # created on Q2
```

**Important if upgrading from v0.3.x:** Delete the old `.weather-view` folder from the SD card. Otherwise Q2 Pod may still show the previously installed **Weather Dashboard** as a second app. Do **not** delete `.weather/state`; your saved location and offline forecast live there.

Weather is displayed under **Library → Weather**, rather than **Library → Games**. It uses Q2 Pod's supported card-game fullscreen display handoff, but remains in Library with `game=1` and `row=library`.

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

If `build/sysroot` already exists, skip the sysroot command. The Q2 root filesystem is generated from the locally supplied stock firmware or downloaded by the sysroot script. The resulting ZIP is `dist/q2-weather.zip`. **Do not commit stock firmware or the sysroot.**

## Architecture and power

`src/dashboard_q2.c` handles framebuffer painting only on input/data changes, Q2 input datagrams, and a slow 1-second cache check. `src/dashboard_render.c` draws theme-adaptive, high-contrast screens and user-replaceable PNG sprites compiled by `tools/pack_sprites.py`. It uses a proportionally spaced, anti-aliased sans-serif typeface baked into the app at build time. `src/dashboard.c` handles wheel, touch, keyboard and page navigation. `src/main.c`, `src/http.c` and `src/weather.c` provide the internal HTTPS/weather helper and cache.

There is **one registered card app** and two executable processes only while the app is open: the graphical UI and its internal HTTP/weather worker. The worker is not a second Library entry. No continuous animation is used. Measured power usage and physical device behavior still require testing on Q2 hardware.

The cache is stored on the SD card at `.weather/state`, and network diagnostics at `.weather/weather.log`. IP-based location can be approximate or affected by a VPN.

## License and credits

Built for Q2 Pod with thanks to DiamondBond and the Q2 community. The small sprite-rendering approach follows the technical design of [q2-diamond-crush](https://github.com/lampsonlnguyen/q2-diamond-crush). Weather data comes from Open-Meteo.
