# Q2 Weather

Weather for the Shanling Q2 running [Q2 Pod](https://github.com/DiamondBond/q2-pod): your location and one saved city, now and for seven days. A card app, like Mixtape: it lives in **Library → Apps → Weather**, and Q2 Pod draws every page itself, so it looks and moves exactly like the rest of the firmware, in every theme, accent and Light Mode.

**Latest release: v1.4.0** — Weather is a Q2 Pod card app, drawn by the firmware in its own theme. See [CHANGELOG.md](CHANGELOG.md) for the complete release notes.

## Primary and fallback services

| Purpose | Primary | Automatic fallback | When both fail |
| --- | --- | --- | --- |
| Weather and 7-day forecast | [Open-Meteo Forecast](https://open-meteo.com/en/docs) | [MET Norway Locationforecast 2.0](https://api.met.no/weatherapi/locationforecast/2.0/documentation) | Retain previously cached weather; no fresh forecast until a provider is reachable |
| Approximate Current Location by public IP | [ipwho.is](https://ipwho.is/) | [ipapi.co](https://ipapi.co/) | Set Home City manually from the Current Location entry or Settings |
| City search for manual selection | [Open-Meteo Geocoding](https://open-meteo.com/en/docs/geocoding-api) | [OpenStreetMap Nominatim](https://nominatim.org/) | Keep any previously selected location; retry search when connectivity returns |
| Saved Location weather | Stored latitude/longitude → Open-Meteo | Same stored coordinates → MET Norway | Keep the Saved Location and cached forecast; **never requires Current Location detection** |

### How location fallback works

- **Current Location:** Automatic location is approximate (based on the public IP address, not GPS). If both IP-location services fail, open **My Location**, or **Settings → Home City**, and type a city in Q2 Pod's own search box.
- **Manual Home City:** The chosen city's coordinates persist across restarts and prevent automatic IP detection from replacing it. To return to automatic detection, remove `.weather/manual-home` from the microSD card.
- **Saved Location:** Uses its stored coordinates directly. Its weather request runs **before** IP-based Current Location detection, so an IP service outage does not block Saved Location.
- **Forecast fallback:** Open-Meteo is tried first. When it fails or returns incomplete daily data, the app tries MET Norway. MET Norway's UTC time series is grouped into seven daily forecasts, so its daily high/low values and day boundaries may differ from Open-Meteo. Precipitation percentages that the fallback does not provide are displayed as unavailable rather than guessed.
- **Offline:** A manual city choice still requires the city-search API to be reachable (Open-Meteo or Nominatim). Selecting a city cannot restore weather forecasts when *both* forecast providers are unreachable. Existing cached forecasts remain available.
- **Provider attribution:** Fallback forecast data © Norwegian Meteorological Institute (MET Norway), CC BY 4.0. Place-search fallback data © OpenStreetMap contributors, ODbL.

## Pages

```
Weather                     🔍 finds a Saved City (the title bar's glass)
├── Westminster             My Location · 22° Mostly clear
├── Ho Chi Minh City        Saved City · 31° Overcast
└── Settings
```

A place's page has its weather now (temperature, condition, today's high and low, humidity), then a row a day for seven days (condition, high / low, chance of rain), and **Updated** with the time of the last update; pressing it updates again. Each place and day row has a small condition picture beside it, as an album has its cover.

| Settings row | Does |
| --- | --- |
| Temperature | Celsius or Fahrenheit |
| Home City | Automatic (by your internet address), or a city you choose |
| Find My Location Automatically | Shown with a chosen Home City: goes back to automatic |
| Saved City | Choose or change it |
| Remove Saved City | Shown with one |
| Update Now | Updates both places; its subtitle is the last update |

Choosing a city: press **City name:**, type in Q2 Pod's search box, and pick the place from the matches (each with its region and country). The click wheel, touch, scrolling, the search box and the title bar are all Q2 Pod's own.

Weather updates both places when its page opens and when asked. Its row opens at once, before Smart Wi-Fi has its link, so a first update that finds no network is tried again a few times. With no network the last forecast stays on the page. Weather never turns Wi-Fi on or off; with Wi-Fi off in the Q2's settings it says so.

## Install or upgrade

Download the latest `q2-weather.zip` from [GitHub Releases](https://github.com/lampsonlnguyen/q2-weather/releases) and extract it **to the root of your Q2 microSD card**. It holds one folder:

```text
.weather/
├── q2app                  # name=Weather, search=City name, row=library
├── run                    # Q2 Pod's launcher for the app
├── q2-weather             # the app: Weather's pages and its HTTPS requests
├── icons/*.jpg            # the condition pictures beside the rows
├── icon.png               # the app's icon
├── state                  # created on Q2; keep it when upgrading
├── units                  # created on Q2
└── manual-home            # created after choosing a Home City
```

**Upgrading from v1.3 or older:** the old full-screen version's `q2-weather-service` and `lang/` are no longer used and can be deleted. Keep `state`: your places and the last forecast live there. Delete an old `.weather-view` folder too, if it is still there.

The app needs a Q2 Pod with card apps (Library → Apps); the condition pictures need one whose card apps show covers (the `art` word).

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

## How it works

Weather is a Q2 Pod card app (Q2 Pod's `patch/addon.c`; the protocol is Q2SPOT/1 in its `docs/librespot.md`). Q2 Pod runs `.weather/run` when the page opens; it copies the app to `/tmp` and keeps it running, so the card stays free for USB storage. The app answers each page on `/tmp/q2-weather.browse.sock` with rows, and Q2 Pod draws them. It draws nothing itself: no framebuffer, fonts or input of its own. Ten minutes after the last page it ends, so Smart Wi-Fi can let the radio sleep.

- `src/main.c`: the pages (menu, a place, settings, city search) and the socket.
- `src/weather.c`, `src/http.c`: the forecast, location and search requests, the fallbacks and the cache.
- `src/art.c`: a row's condition picture, copied into Q2 Pod's art folder (`/tmp/q2-weather.art/<key>.jpg`, the key FNV-1a of the row's uri and context) and renamed into place.
- `tools/draw_icons.py`: draws the condition pictures at build time, 64 px, the size Q2 Pod shows a row's cover (`make icons`; needs Pillow).
- `tests/test_pages.py`: runs the host build on a cached forecast and checks every page's rows (`make check`).

The cache is stored on the SD card at `.weather/state`, and network diagnostics at `.weather/weather.log`. IP-based location can be approximate or affected by a VPN.

## License and credits

Built for Q2 Pod with thanks to DiamondBond and the Q2 community. Its pages are drawn by Q2 Pod itself, in whichever theme is chosen. Weather forecasts use Open-Meteo with MET Norway as a fallback. IP-based location uses ipwho.is with ipapi.co as fallback; city search uses Open-Meteo geocoding with OpenStreetMap Nominatim as fallback.

### Weather art

The condition pictures are original drawings made by `tools/draw_icons.py` at build time, under this repository's license. `assets/icon.png` is the app's icon.
