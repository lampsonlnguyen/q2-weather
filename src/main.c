/* SPDX-License-Identifier: MIT */
#include "art.h"
#include "http.h"
#include "weather.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/q2-weather.browse.sock"
#define STATE_PATH "/mnt/mmc/.weather/state"
#define STATE_TEMP "/mnt/mmc/.weather/state.tmp"
#define MANUAL_HOME_PATH "/mnt/mmc/.weather/manual-home"
#define UNITS_PATH "/mnt/mmc/.weather/units"
#define UNITS_TEMP "/mnt/mmc/.weather/units.tmp"
#define CAPS_PATH "/tmp/q2-addon.caps" /* the firmware's card-app words */
#define WIRE_MAX 8192
#define ROW_MAX 32768

typedef struct {
    WeatherApp app;
    int state;
    int pending;
    int error;
    int units_f;
    char search[80];
    WeatherGeoResult search_matches[WEATHER_GEO_MAX];
    int search_count;
    int search_pending, search_found, search_generation;
    int generation, rerun;
    int art; /* the firmware draws row covers */
} Runtime;

typedef struct {
    char rows[ROW_MAX];
    size_t used;
    int count;
    char message[128];
    char status[16];
} Reply;

static Runtime rt;

/* A path as the Q2 has it; on a computer (the host checks) under $WEATHER_ROOT. */
#ifdef HOST
static const char *R(const char *path) {
    static char buf[4][512];
    static int at;
    const char *root = getenv("WEATHER_ROOT");
    if (!root) return path;
    at = (at + 1) % 4;
    snprintf(buf[at], sizeof buf[at], "%s%s", root, path);
    return buf[at];
}
#else
#define R(path) (path)
#endif
static volatile sig_atomic_t stopped;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

static void stop_server(int signo) {
    (void)signo;
    stopped = 1;
}

static int escape(char *dst, size_t cap, const char *src) {
    size_t at = 0;
    for (; *src; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '%' || c == '\t' || c == '\n' || c == '\r') {
            if (at + 3 >= cap) return -1;
            at += (size_t)snprintf(dst + at, cap - at, "%%%02X", c);
        } else {
            if (at + 1 >= cap) return -1;
            dst[at++] = (char)c;
        }
    }
    if (at >= cap) return -1;
    dst[at] = 0;
    return (int)at;
}

static int unescape(char *p) {
    char *out = p;
    while (*p) {
        if (*p != '%') {
            *out++ = *p++;
            continue;
        }
        if (!p[1] || !p[2]) return -1;
        char digits[3] = { p[1], p[2], 0 };
        char *end = NULL;
        long value = strtol(digits, &end, 16);
        if (*end || value < 1 || value > 255) return -1;
        *out++ = (char)value;
        p += 3;
    }
    *out = 0;
    return 0;
}

static void row(Reply *reply, const char *kind, const char *uri, const char *title,
                const char *subtitle, const char *context) {
    if (reply->count >= 50) return;
    const char *parts[] = { kind, uri, title, subtitle, context, "" };
    char escaped[6][1024];
    for (int i = 0; i < 6; i++) {
        if (escape(escaped[i], sizeof escaped[i], parts[i]) < 0) return;
    }
    size_t left = sizeof reply->rows - reply->used;
    int n = snprintf(reply->rows + reply->used, left, "%s\t%s\t%s\t%s\t%s\t%s\n", escaped[0],
                     escaped[1], escaped[2], escaped[3], escaped[4], escaped[5]);
    if (n <= 0 || (size_t)n >= left) return;
    reply->used += (size_t)n;
    reply->count++;
}

static void info(Reply *reply, const char *title, const char *subtitle) {
    row(reply, "info", "", title, subtitle, "");
}

static int load_state(void) {
    char buf[2048];
    FILE *f = fopen(R(STATE_PATH), "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return weather_read(&rt.app, buf);
}

static void save_state(void) {
    char buf[2048];
    int n = weather_write(&rt.app, buf, sizeof buf);
    if (n < 0) return;
    FILE *f = fopen(R(STATE_TEMP), "wb");
    if (!f) return;
    int ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n && !fflush(f) && !fsync(fileno(f));
    if (fclose(f)) ok = 0;
    if (!ok || rename(R(STATE_TEMP), R(STATE_PATH))) unlink(R(STATE_TEMP));
}

/* Manual Home remains selected across refreshes and restarts. */
static int manual_home_enabled(void) { return access(R(MANUAL_HOME_PATH), F_OK) == 0; }

static int enable_manual_home(void) {
    FILE *f = fopen(R(MANUAL_HOME_PATH), "wb");
    if (!f) return -1;
    int ok = fputc('1', f) != EOF;
    if (fclose(f)) ok = 0;
    return ok ? 0 : -1;
}

static int wifi_preference_on(void) {
    FILE *f = fopen(R("/mnt/data/config.ini"), "rb");
    if (!f) return 1;
    char line[256];
    int in = 0, enabled = 1;
    while (fgets(line, sizeof line, f)) {
        if (*line == '[') in = !strncmp(line, "[SYSSET]", 8);
        if (in && !strncmp(line, "WIFI=", 5)) enabled = atoi(line + 5) != 0;
    }
    fclose(f);
    return enabled;
}

/* An already-running Q2 Pod card app keeps Smart Wi-Fi awake through
 * addon_running(). Never bypass the user's explicit Wi-Fi-off preference.
 * Stock firmware has no published standalone-app radio-switch ABI. */
static int wifi_state(void *unused) {
    (void)unused;
#ifdef HOST /* the page checks run with no network */
    if (getenv("WEATHER_NO_NET")) return -1;
#endif
    return wifi_preference_on() ? 1 : -1;
}

static int wifi_power(void *unused, int enable) {
    (void)unused;
    (void)enable;
    return -1;
}

/* A failed IP location API must never block saved-city weather requests. */
static int wait_online(void *unused, int timeout_seconds) {
    (void)unused;
    (void)timeout_seconds;
    return stopped ? -1 : 0;
}

static int detect(void *unused, char *name, size_t cap, double *lat, double *lon) {
    (void)unused;
    return weather_geolocate(name, cap, lat, lon);
}

typedef struct {
    double lat;
    double lon;
    int ready;
    WeatherDay days[Q2_FORECAST_DAYS];
} ForecastCache;
static ForecastCache fc;

static int weather_current(void *unused, double lat, double lon, int *temp, int *humidity,
                           int *code, long *updated) {
    (void)unused;
    char dates[Q2_FORECAST_DAYS][11];
    int highs[Q2_FORECAST_DAYS], lows[Q2_FORECAST_DAYS], rain[Q2_FORECAST_DAYS];
    int codes[Q2_FORECAST_DAYS];
    fc.ready = 0;
    int result = weather_request(lat, lon, temp, humidity, code, updated, dates, highs, lows, rain,
                                 codes, Q2_FORECAST_DAYS);
    if (result < 0) return -1;
    if (result > 0) return 0; /* Current conditions succeeded without daily data. */
    for (int i = 0; i < Q2_FORECAST_DAYS; i++) {
        WeatherDay *day = &fc.days[i];
        snprintf(day->date, sizeof day->date, "%s", dates[i]);
        day->high_c = highs[i];
        day->low_c = lows[i];
        day->precipitation_pct = rain[i];
        day->weather_code = codes[i];
    }
    fc.lat = lat;
    fc.lon = lon;
    fc.ready = 1;
    return 0;
}

static int weather_daily(void *unused, double lat, double lon, WeatherDay *days, int count) {
    (void)unused;
    if (!fc.ready || fc.lat != lat || fc.lon != lon || count != Q2_FORECAST_DAYS) return -1;
    memcpy(days, fc.days, sizeof fc.days);
    return 0;
}

static const WeatherBackend backend = { NULL,   wifi_state,      wifi_power,   wait_online,
                                        detect, weather_current, weather_daily };

/* Network calls run outside the Library browser's 15-second request timeout. */
static void *refresh_worker(void *unused) {
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&state_mutex);
        WeatherApp snapshot = rt.app;
        int generation = rt.generation;
        pthread_mutex_unlock(&state_mutex);
        int error = weather_refresh(&snapshot, &backend, !manual_home_enabled());
        pthread_mutex_lock(&state_mutex);
        if (generation == rt.generation) {
            rt.app = snapshot;
            rt.error = error;
            save_state();
        } else {
            rt.rerun = 1;
        }
        int again = rt.rerun;
        rt.rerun = 0;
        if (!again) rt.pending = 0;
        pthread_mutex_unlock(&state_mutex);
        if (!again) break;
    }
    return NULL;
}

static void refresh(void) {
    pthread_mutex_lock(&state_mutex);
    if (rt.pending) {
        rt.rerun = 1;
        pthread_mutex_unlock(&state_mutex);
        return;
    }
    rt.pending = 1;
    pthread_mutex_unlock(&state_mutex);
    pthread_t thread;
    if (pthread_create(&thread, NULL, refresh_worker, NULL)) {
        pthread_mutex_lock(&state_mutex);
        rt.pending = 0;
        rt.error = WEATHER_FETCH;
        pthread_mutex_unlock(&state_mutex);
    } else {
        pthread_detach(thread);
    }
}

/* City lookup also runs on a worker: field edits never block Q2 Pod. */
static void *search_worker(void *unused) {
    (void)unused;
    char query[sizeof rt.search];
    pthread_mutex_lock(&state_mutex);
    snprintf(query, sizeof query, "%s", rt.search);
    int generation = rt.search_generation;
    pthread_mutex_unlock(&state_mutex);
    WeatherGeoResult matches[WEATHER_GEO_MAX] = { 0 };
    int count = weather_geocode_many(query, matches);
    pthread_mutex_lock(&state_mutex);
    if (generation == rt.search_generation) {
        rt.search_count = count > 0 ? count : 0;
        rt.search_found = count > 0;
        if (count > 0) memcpy(rt.search_matches, matches, (size_t)count * sizeof matches[0]);
        rt.search_pending = 0;
    }
    pthread_mutex_unlock(&state_mutex);
    return NULL;
}

static void start_search(const char *query) {
    pthread_mutex_lock(&state_mutex);
    snprintf(rt.search, sizeof rt.search, "%s", query);
    rt.search_generation++;
    rt.search_pending = 1;
    rt.search_found = 0;
    rt.search_count = 0;
    pthread_mutex_unlock(&state_mutex);
    pthread_t thread;
    if (pthread_create(&thread, NULL, search_worker, NULL)) {
        pthread_mutex_lock(&state_mutex);
        rt.search_pending = 0;
        pthread_mutex_unlock(&state_mutex);
    } else {
        pthread_detach(thread);
    }
}

/* ---- Pages ----------------------------------------------------------------------------------
 * Every page is rows that Q2 Pod draws itself (its patch/addon.c), so Weather takes the
 * firmware's theme, accent, Light Mode, fonts, wheel and touch as they are. A row's cover is a
 * condition tile from .weather/icons (art.c), shown when the firmware lists "art". */

#define DOT " \xc2\xb7 " /* " · ", as Q2 Pod's own apps separate a subtitle's parts */
#define DEG "\xc2\xb0"

static int load_units(void) {
    FILE *f = fopen(R(UNITS_PATH), "rb");
    if (!f) return 0;
    int unit = fgetc(f) == '1';
    fclose(f);
    return unit;
}

static void save_units(int unit) {
    FILE *f = fopen(R(UNITS_TEMP), "wb");
    if (!f) return;
    int ok = fputc(unit ? '1' : '0', f) != EOF && !fflush(f) && !fsync(fileno(f));
    if (fclose(f)) ok = 0;
    if (!ok || rename(R(UNITS_TEMP), R(UNITS_PATH))) unlink(R(UNITS_TEMP));
}

static int has_cap(const char *word) {
    char caps[1024] = " ", want[40];
    FILE *f = fopen(R(CAPS_PATH), "rb");
    size_t n = f ? fread(caps + 1, 1, sizeof caps - 3, f) : 0;
    if (f) fclose(f);
    caps[n + 1] = ' ';
    caps[n + 2] = 0;
    for (char *c = caps; *c; ++c)
        if (*c == '\n' || *c == '\r' || *c == '\t') *c = ' ';
    snprintf(want, sizeof want, " %s ", word);
    return strstr(caps, want) != NULL;
}

static void degrees(char *out, size_t cap, int celsius) {
    int value = rt.units_f ? (celsius * 9 + (celsius >= 0 ? 2 : -2)) / 5 + 32 : celsius;
    snprintf(out, cap, "%d" DEG, value);
}

/* A cover for the row (uri, context) when the firmware draws covers. The context carries the
 * condition code, so a new condition is a new file rather than one rewritten under the page. */
static void cover(const char *uri, char *context, size_t cap, const char *what, int code) {
    snprintf(context, cap, "%s %d", what, code);
    if (rt.art) weather_art_row(uri, context, code);
}

static const char *day_name(int index, const char *date, char *out, size_t cap) {
    struct tm tm = { 0 };
    if (index == 0) return "Today";
    if (index == 1) return "Tomorrow";
    if (sscanf(date, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return date;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_hour = 12;
    if (mktime(&tm) == (time_t)-1) return date;
    strftime(out, cap, "%A %e", &tm);
    for (char *c = out; (c = strstr(c, "  "));) memmove(c, c + 1, strlen(c));
    return out;
}

static void updated_text(char *out, size_t cap, long at) {
    time_t t = (time_t)at;
    struct tm local;
    if (at <= 0 || !localtime_r(&t, &local)) {
        snprintf(out, cap, "Not yet");
        return;
    }
    time_t now = time(NULL);
    struct tm today;
    localtime_r(&now, &today);
    if (local.tm_yday == today.tm_yday && local.tm_year == today.tm_year)
        strftime(out, cap, "Today, %H:%M", &local);
    else
        strftime(out, cap, "%b %e, %H:%M", &local);
}

static void snapshot(WeatherApp *app, int *pending, int *error) {
    pthread_mutex_lock(&state_mutex);
    *app = rt.app;
    *pending = rt.pending;
    *error = rt.error;
    pthread_mutex_unlock(&state_mutex);
}

/* Why a location has no weather: Wi-Fi off, no answer yet, or no answer at all. */
static const char *why_none(int pending, int error) {
    if (pending) return "Updating\xe2\x80\xa6";
    if (!wifi_preference_on()) return "Turn on Wi-Fi to get the weather";
    if (error) return "Couldn't reach the weather service";
    return "No weather yet";
}

/* The first level: a row for each place, Settings last. A first Search row is the title bar's
 * magnifying glass (Q2 Pod's search=, here "City name"): it finds the Saved City. */
static void page_menu(Reply *r) {
    WeatherApp app;
    int pending, error;
    snapshot(&app, &pending, &error);
    char sub[192], temp[16], ctx[32];
    row(r, "search", "", "Find a City", "", "");
    for (int i = 0; i < Q2_WEATHER_SLOTS; i++) {
        WeatherLocation *l = &app.locations[i];
        const char *where = i ? "Saved City" : manual_home_enabled() ? "Home City" : "My Location";
        if (!l->used) {
            if (i)
                row(r, "list", "cities", "Add a City", "Search for a place by name", "1");
            else
                row(r, "list", "cities", "My Location",
                    pending ? "Finding where you are\xe2\x80\xa6" : "Choose your Home City", "0");
            continue;
        }
        if (l->valid) {
            degrees(temp, sizeof temp, l->temperature_c);
            snprintf(sub, sizeof sub, "%s" DOT "%s %s", where, temp,
                     weather_condition(l->weather_code));
            cover("location", ctx, sizeof ctx, i ? "1" : "0", l->weather_code);
        } else {
            snprintf(sub, sizeof sub, "%s" DOT "%s", where, why_none(pending, error));
            snprintf(ctx, sizeof ctx, "%d", i);
        }
        row(r, "list", "location", l->name, sub, ctx);
    }
    row(r, "list", "settings", "Settings", "", "");
    if (pending) snprintf(r->message, sizeof r->message, "refresh");
}

/* A place: now, then a row a day. */
static void page_location(Reply *r, const char *arg) {
    WeatherApp app;
    int pending, error, slot = atoi(arg) == 1;
    snapshot(&app, &pending, &error);
    WeatherLocation *l = &app.locations[slot];
    char title[96], sub[192], hi[16], lo[16], name[48], ctx[64];
    if (pending) snprintf(r->message, sizeof r->message, "refresh");
    if (!l->used) {
        info(r, slot ? "No Saved City" : "Where you are isn't known",
             slot ? "Add one from Weather's first page" : "Choose your Home City in Settings");
        return;
    }
    if (l->valid) {
        degrees(hi, sizeof hi, l->temperature_c);
        snprintf(title, sizeof title, "%s %s", hi, weather_condition(l->weather_code));
        if (l->forecast_valid) {
            degrees(hi, sizeof hi, l->days[0].high_c);
            degrees(lo, sizeof lo, l->days[0].low_c);
            snprintf(sub, sizeof sub, "H %s  L %s" DOT "Humidity %d%%", hi, lo, l->humidity);
        } else
            snprintf(sub, sizeof sub, "Humidity %d%%", l->humidity);
        cover("now", ctx, sizeof ctx, slot ? "1" : "0", l->weather_code);
        row(r, "info", "now", title, sub, ctx);
    } else
        info(r, l->name, why_none(pending, error));
    if (l->forecast_valid) {
        for (int i = 0; i < Q2_FORECAST_DAYS; i++) {
            WeatherDay *d = &l->days[i];
            char what[32];
            degrees(hi, sizeof hi, d->high_c);
            degrees(lo, sizeof lo, d->low_c);
            if (d->precipitation_pct >= 0)
                snprintf(sub, sizeof sub, "%s" DOT "%s / %s" DOT "Rain %d%%",
                         weather_condition(d->weather_code), hi, lo, d->precipitation_pct);
            else
                snprintf(sub, sizeof sub, "%s" DOT "%s / %s", weather_condition(d->weather_code),
                         hi, lo);
            snprintf(what, sizeof what, "%d %d", slot, i);
            cover("day", ctx, sizeof ctx, what, d->weather_code);
            row(r, "info", "day", day_name(i, d->date, name, sizeof name), sub, ctx);
        }
    }
    if (l->valid) {
        updated_text(sub, sizeof sub, l->updated_at);
        row(r, "action", "refresh", pending ? "Updating\xe2\x80\xa6" : "Updated", sub, "");
    }
}

/* Search results as rows that save the place to slot. */
static void result_rows(Reply *r, int slot, const char *query) {
    WeatherGeoResult matches[WEATHER_GEO_MAX];
    int pending, count;
    char said[sizeof rt.search];
    pthread_mutex_lock(&state_mutex);
    pending = rt.search_pending;
    count = rt.search_count;
    snprintf(said, sizeof said, "%s", rt.search);
    if (count > 0) memcpy(matches, rt.search_matches, (size_t)count * sizeof matches[0]);
    pthread_mutex_unlock(&state_mutex);
    if (!said[0] || (query && strcmp(query, said))) return;
    if (pending) {
        info(r, "Searching\xe2\x80\xa6", "");
        snprintf(r->message, sizeof r->message, "refresh");
        return;
    }
    if (count <= 0) {
        info(r, "No place has that name",
             wifi_preference_on() ? "Check the spelling" : "Turn on Wi-Fi to search");
        return;
    }
    for (int i = 0; i < count; ++i) {
        char ctx[96], where[160];
        snprintf(ctx, sizeof ctx, "%d %.6f %.6f", slot, matches[i].latitude, matches[i].longitude);
        snprintf(where, sizeof where, "%s%s%s", matches[i].region,
                 matches[i].region[0] && matches[i].country[0] ? ", " : "", matches[i].country);
        row(r, "action", "savecity", matches[i].name, where, ctx);
    }
}

static void start_search_once(const char *query) {
    pthread_mutex_lock(&state_mutex);
    int same = !strcmp(rt.search, query);
    pthread_mutex_unlock(&state_mutex);
    if (!same) start_search(query);
}

/* Choosing a place for slot 0 (Home City) or 1 (Saved City): a field row whose text Q2 Pod sends
 * back to this level ("cities <slot> <text>"), then the matches under it. */
static void page_cities(Reply *r, const char *slot_arg, const char *query) {
    int slot = atoi(slot_arg) == 1;
    char box[sizeof rt.search];
    if (query && *query) start_search_once(query);
    pthread_mutex_lock(&state_mutex);
    snprintf(box, sizeof box, "%s", rt.search);
    pthread_mutex_unlock(&state_mutex);
    row(r, "field", "cities", "City name:", box[0] ? box : "Type a place to search for",
        slot ? "1" : "0");
    result_rows(r, slot, NULL);
}

static void page_settings(Reply *r) {
    WeatherApp app;
    int pending, error;
    snapshot(&app, &pending, &error);
    int manual = manual_home_enabled();
    char sub[160];
    row(r, "action", "units", "Temperature", rt.units_f ? "Fahrenheit" : "Celsius", "");
    row(r, "list", "cities", "Home City",
        manual && app.locations[0].used ? app.locations[0].name : "Automatic, from your Wi-Fi",
        "0");
    if (manual)
        row(r, "action", "autohome", "Find My Location Automatically",
            "By your internet address, not GPS", "");
    row(r, "list", "cities", "Saved City", app.locations[1].used ? app.locations[1].name : "None",
        "1");
    if (app.locations[1].used) row(r, "action", "clear", "Remove Saved City", "", "");
    long last = app.locations[0].updated_at > app.locations[1].updated_at
                    ? app.locations[0].updated_at
                    : app.locations[1].updated_at;
    updated_text(sub, sizeof sub, last);
    row(r, "action", "refresh", pending ? "Updating\xe2\x80\xa6" : "Update Now", sub, "");
    if (!wifi_preference_on())
        info(r, "Wi-Fi is off", "Weather shows what it last got");
    else if (error && !pending)
        info(r, "Last update failed", "Details in .weather/weather.log");
    info(r, "Forecasts", "Open-Meteo" DOT "MET Norway, CC BY 4.0");
    info(r, "Places", "Open-Meteo" DOT "OpenStreetMap, ODbL");
    /* asked again every 2 s, so a choice's subtitle and an update show at once */
    snprintf(r->message, sizeof r->message, "refresh");
}

static void save_city(Reply *r, const char *arg) {
    double lat, lon;
    int slot = 1, found = 0;
    char city[Q2_WEATHER_NAME] = { 0 };
    pthread_mutex_lock(&state_mutex);
    int fields = sscanf(arg, "%d %lf %lf", &slot, &lat, &lon);
    if (fields == 3 && (slot == 0 || slot == 1) && !rt.search_pending) {
        for (int i = 0; i < rt.search_count; ++i) {
            WeatherGeoResult *item = &rt.search_matches[i];
            if (fabs(lat - item->latitude) < 0.000001 && fabs(lon - item->longitude) < 0.000001) {
                snprintf(city, sizeof city, "%.63s", item->name);
                found = 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&state_mutex);
    if (!found) {
        snprintf(r->message, sizeof r->message, "Search again: the results changed");
        return;
    }
    pthread_mutex_lock(&state_mutex);
    int good = !(slot == 0 && enable_manual_home()) &&
               weather_set_location(&rt.app, slot, city, lat, lon) == WEATHER_OK;
    if (good) {
        rt.generation++;
        save_state();
    }
    pthread_mutex_unlock(&state_mutex);
    if (good) {
        refresh();
        snprintf(r->message, sizeof r->message, "%.60s is your %s", city,
                 slot ? "Saved City" : "Home City");
    } else
        snprintf(r->message, sizeof r->message, "Can't save that place");
}

static void handle(char **args, int count, Reply *r) {
    const char *op = count > 1 ? args[1] : "";
    const char *arg = count > 2 ? args[2] : "";
    snprintf(r->status, sizeof r->status, "ok");
    rt.art = has_cap("art");
    if (!strcmp(op, "menu"))
        page_menu(r);
    else if (!strcmp(op, "location"))
        page_location(r, arg);
    else if (!strcmp(op, "settings"))
        page_settings(r);
    else if (!strcmp(op, "cities"))
        page_cities(r, arg, count > 3 ? args[3] : NULL);
    else if (!strcmp(op, "search")) { /* the title bar's glass: a Saved City */
        start_search_once(arg);
        result_rows(r, 1, arg);
    } else if (!strcmp(op, "savecity"))
        save_city(r, arg);
    else if (!strcmp(op, "clear")) {
        pthread_mutex_lock(&state_mutex);
        memset(&rt.app.locations[1], 0, sizeof rt.app.locations[1]);
        rt.generation++;
        save_state();
        pthread_mutex_unlock(&state_mutex);
        snprintf(r->message, sizeof r->message, "Saved City removed");
    } else if (!strcmp(op, "autohome")) {
        unlink(R(MANUAL_HOME_PATH));
        refresh();
        snprintf(r->message, sizeof r->message, "Finding your location");
    } else if (!strcmp(op, "units")) {
        pthread_mutex_lock(&state_mutex);
        rt.units_f = !rt.units_f;
        pthread_mutex_unlock(&state_mutex);
        save_units(rt.units_f);
        /* no message: the live list shows the new choice at once */
    } else if (!strcmp(op, "refresh")) {
        refresh();
    } else
        snprintf(r->status, sizeof r->status, "unsupported");
}

static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, data + sent, len - sent);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static void answer(int socket_fd) {
    char request[WIRE_MAX + 1], header[512], msg[384];
    Reply reply = { 0 };
    struct timeval timeout = { 5, 0 };
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    int n = 0;
    ssize_t got;
    while (n < WIRE_MAX && (got = read(socket_fd, request + n, (size_t)(WIRE_MAX - n))) > 0)
        n += (int)got;
    request[n] = 0;
    char *fields[12] = { 0 };
    int fields_count = 0;
    if (!strncmp(request, "Q2SPOT/1\n", 9)) {
        char *line = request + 9;
        line[strcspn(line, "\n")] = 0;
        char *cursor = line;
        while (fields_count < 12 && cursor) { fields[fields_count++] = strsep(&cursor, "\t"); }
        for (int i = 0; i < fields_count; i++) {
            if (unescape(fields[i])) fields_count = 0;
        }
    }
    if (fields_count >= 2)
        handle(fields, fields_count, &reply);
    else
        snprintf(reply.status, sizeof reply.status, "bad");
    if (escape(msg, sizeof msg, reply.message) < 0) msg[0] = 0;
    const char *id = fields_count ? fields[0] : "0";
    int hn = snprintf(header, sizeof header, "Q2SPOT/1\n%.20s\t%s\t%s\t\n", id, reply.status, msg);
    if (hn > 0 && hn < (int)sizeof header) {
        if (!send_all(socket_fd, header, (size_t)hn) && reply.used)
            (void)send_all(socket_fd, reply.rows, reply.used);
    }
    close(socket_fd);
}

int main(void) {
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGPIPE, SIG_IGN);
    weather_init(&rt.app);
    load_state();
    rt.units_f = load_units();
    weather_art_init();
    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", R(SOCKET_PATH));
    unlink(R(SOCKET_PATH));
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof addr) || listen(fd, 4)) {
        if (fd >= 0) close(fd);
        return 1;
    }
    /* Weather's row is in Library's Apps, which opens at once rather than waiting for Smart
     * Wi-Fi's link, so the first update may find no network: a failed one is tried again a few
     * times, 10 s apart, while the page is open. */
    refresh();
    time_t last = time(NULL), tried = last;
    int retries = 0;
    while (!stopped && time(NULL) - last < 600) {
        pthread_mutex_lock(&state_mutex);
        int again = !rt.pending && rt.error && retries < 3 && time(NULL) - tried >= 10;
        pthread_mutex_unlock(&state_mutex);
        if (again) {
            ++retries;
            tried = time(NULL);
            refresh();
        }
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 1000) > 0) {
            int accepted = accept(fd, NULL, NULL);
            if (accepted >= 0) {
                answer(accepted);
                last = time(NULL);
            }
        }
    }
    close(fd);
    unlink(R(SOCKET_PATH));
    weather_art_cleanup();
    return 0;
}
