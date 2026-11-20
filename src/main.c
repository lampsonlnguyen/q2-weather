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
#define STATUS_PATH "/mnt/mmc/.weather/network-status"
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
    time_t last_attempt;
    int ever_refreshed;
} Runtime;

typedef struct {
    char rows[ROW_MAX];
    size_t used;
    int count;
    char message[128];
    char status[16];
} Reply;

static Runtime rt;
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
        char digits[3] = {p[1], p[2], 0};
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
    const char *parts[] = {kind, uri, title, subtitle, context, ""};
    char escaped[6][1024];
    for (int i = 0; i < 6; i++) {
        if (escape(escaped[i], sizeof escaped[i], parts[i]) < 0) return;
    }
    size_t left = sizeof reply->rows - reply->used;
    int n = snprintf(reply->rows + reply->used, left, "%s\t%s\t%s\t%s\t%s\t%s\n",
                     escaped[0], escaped[1], escaped[2], escaped[3], escaped[4], escaped[5]);
    if (n <= 0 || (size_t)n >= left) return;
    reply->used += (size_t)n;
    reply->count++;
}

static void info(Reply *reply, const char *title, const char *subtitle) {
    row(reply, "info", "", title, subtitle, "");
}

static int load_state(void) {
    char buf[2048];
    FILE *f = fopen(STATE_PATH, "rb");
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
    FILE *f = fopen(STATE_TEMP, "wb");
    if (!f) return;
    int ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n && !fflush(f) &&
             !fsync(fileno(f));
    if (fclose(f)) ok = 0;
    if (!ok || rename(STATE_TEMP, STATE_PATH)) unlink(STATE_TEMP);
}

/* A manually selected Home location must survive restarts and must not be
 * overwritten by subsequent approximate IP geolocation requests. */
static int manual_home_enabled(void) {
    return access(MANUAL_HOME_PATH, F_OK) == 0;
}

static int enable_manual_home(void) {
    FILE *file = fopen(MANUAL_HOME_PATH, "wb");
    if (!file) return -1;
    int ok = fputc('1', file) != EOF;
    if (fclose(file)) ok = 0;
    return ok ? 0 : -1;
}

static int wifi_preference_on(void) {
    FILE *f = fopen("/mnt/data/config.ini", "rb");
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
    return wifi_preference_on() ? 1 : -1;
}

static int wifi_power(void *unused, int enable) {
    (void)unused;
    (void)enable;
    return -1;
}

/* Do not use the IP geolocation provider as a network preflight.
 * It can fail independently of Open-Meteo and must not prevent a saved
 * location from receiving a forecast. Each request checks connectivity. */
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
    int result = weather_request(lat, lon, temp, humidity, code, updated, dates, highs,
                                 lows, rain, codes, Q2_FORECAST_DAYS);
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

static const WeatherBackend backend = {
    NULL, wifi_state, wifi_power, wait_online, detect, weather_current, weather_daily
};

/* Network calls run outside the Library browser's 15-second request timeout. */
static void save_network_status(int error) {
    const char *message = "";
    if (error != WEATHER_OK) {
        const char *http_error = weather_http_error();
        if (strstr(http_error, "DNS unavailable"))
            message = "DNS unavailable - retrying";
        else if (strstr(http_error, "Connection timed out"))
            message = "Connection timed out - retrying";
        else if (strstr(http_error, "TLS certificate error"))
            message = "Secure connection failed";
        else if (error == WEATHER_GEO)
            message = "Location unavailable - set Home City";
        else if (error == WEATHER_RADIO)
            message = "Wi-Fi disabled";
        else
            message = "Weather update failed - retrying";
    }
    FILE *file = fopen(STATUS_PATH, "wb");
    if (!file) return;
    (void)fprintf(file, "%s\n", message);
    (void)fclose(file);
}

/* Network refresh happens on a worker, never the rendering thread. */
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
            save_network_status(error);
            rt.last_attempt = time(NULL);
            if (error == WEATHER_OK) rt.ever_refreshed = 1;
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
    rt.last_attempt = time(NULL);
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
    WeatherGeoResult matches[WEATHER_GEO_MAX] = {0};
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

static void display_number(char *out, size_t cap, int celsius) {
    int value = rt.units_f ? (celsius * 9 + (celsius >= 0 ? 2 : -2)) / 5 + 32 : celsius;
    snprintf(out, cap, "%d%c", value, rt.units_f ? 'F' : 'C');
}

static void page_location(Reply *r, int slot) {
    WeatherLocation location;
    pthread_mutex_lock(&state_mutex);
    location = rt.app.locations[slot];
    int pending = rt.pending;
    pthread_mutex_unlock(&state_mutex);
    WeatherLocation *loc = &location;
    char temp[32], subtitle[128], date[64], hi[32], lo[32];
    if (pending) {
        info(r, "Updating weather", "Using cached readings while fetching");
        snprintf(r->message, sizeof r->message, "refresh");
    }
    if (!loc->used) {
        info(r, slot ? "No saved city" : "Current city unavailable",
             slot ? "Choose a city below" : "Set Home City in Settings");
    } else {
        if (loc->valid) {
            display_number(temp, sizeof temp, loc->temperature_c);
            snprintf(subtitle, sizeof subtitle, "%s | Humidity %d%%",
                     weather_description(loc->weather_code), loc->humidity);
            char art_context[8];
            snprintf(art_context, sizeof art_context, "%d", slot);
            weather_art_row("hero", art_context, loc->weather_code);
            row(r, "info", "hero", temp, subtitle, art_context);
        } else {
            info(r, loc->name, "No weather downloaded yet");
        }
        if (loc->forecast_valid) {
            for (int i = 0; i < Q2_FORECAST_DAYS; i++) {
                WeatherDay *d = &loc->days[i];
                display_number(hi, sizeof hi, d->high_c);
                display_number(lo, sizeof lo, d->low_c);
                snprintf(date, sizeof date, "%.10s", d->date);
                if (d->precipitation_pct < 0)
                    snprintf(subtitle, sizeof subtitle, "%s / %s | %s", hi, lo,
                             weather_description(d->weather_code));
                else
                    snprintf(subtitle, sizeof subtitle, "%s / %s | Rain %d%% | %s", hi, lo,
                             d->precipitation_pct, weather_description(d->weather_code));
                char art_context[16];
                snprintf(art_context, sizeof art_context, "%d-%d", slot, i);
                weather_art_row("forecast", art_context, d->weather_code);
                row(r, "info", "forecast", date, subtitle, art_context);
            }
        }
        if (loc->valid) {
            char stamp[48];
            time_t updated = (time_t)loc->updated_at;
            struct tm local;
            if (localtime_r(&updated, &local))
                strftime(stamp, sizeof stamp, "%b %d, %I:%M %p", &local);
            else
                snprintf(stamp, sizeof stamp, "Cached reading");
            info(r, "Last updated", stamp);
        }
    }
    if (slot) {
        row(r, "list", "cities", "Change saved city", "Search for a city", "");
        row(r, "action", "clear", "Remove saved city", "", "");
    }
    row(r, "action", "refresh", "Refresh both locations", "Fetch current and 7-day weather", "");
}

static void page_menu(Reply *r) {
    WeatherApp app;
    int pending, error;
    pthread_mutex_lock(&state_mutex);
    app = rt.app;
    pending = rt.pending;
    error = rt.error;
    pthread_mutex_unlock(&state_mutex);
    char sub[128];
    if (pending) {
        info(r, "Updating both locations", "Weather requests in progress");
        snprintf(r->message, sizeof r->message, "refresh");
    }
    for (int i = 0; i < Q2_WEATHER_SLOTS; i++) {
        WeatherLocation *l = &app.locations[i];
        if (l->used && l->valid) {
            char temp[24];
            display_number(temp, sizeof temp, l->temperature_c);
            snprintf(sub, sizeof sub, "%s | %s | %s", l->name, temp,
                     weather_description(l->weather_code));
        } else {
            snprintf(sub, sizeof sub, "%s", l->used ? l->name : "Not yet configured");
        }
        if (l->valid) weather_art_row("location", i ? "1" : "0", l->weather_code);
        row(r, "list", "location", i ? "Saved Location" : "Current Location", sub,
            i ? "1" : "0");
    }
    row(r, "action", "refresh", "Refresh weather", "Update both locations", "");
    row(r, "list", "cities", "Choose saved city", "Enter a city name", "");
    row(r, "action", "units", rt.units_f ? "Use Celsius" : "Use Fahrenheit",
        "Switch displayed temperature units", "");
    if (!wifi_preference_on())
        info(r, "Wi-Fi is off", "Enable Wi-Fi in Q2 settings to refresh");
    else if (error)
        info(r, "Weather may be cached", "Refresh was unsuccessful");
    if (error)
        info(r, "Diagnostics", "See .weather/weather.log on the SD card");
}

static void page_cities(Reply *r) {
    WeatherGeoResult matches[WEATHER_GEO_MAX];
    int pending, count, searched;
    pthread_mutex_lock(&state_mutex);
    pending = rt.search_pending;
    count = rt.search_count;
    searched = rt.search[0] != 0;
    if (count > 0) memcpy(matches, rt.search_matches, (size_t)count * sizeof matches[0]);
    pthread_mutex_unlock(&state_mutex);
    row(r, "field", "citysearch", "City name:", "Enter city to search", "");
    if (pending) {
        info(r, "Searching city", "Checking Open-Meteo");
        snprintf(r->message, sizeof r->message, "refresh");
    } else if (count > 0) {
        for (int i = 0; i < count; ++i) {
            char coord[80], region[160];
            snprintf(coord, sizeof coord, "%.6f %.6f", matches[i].latitude,
                     matches[i].longitude);
            if (matches[i].region[0] && matches[i].country[0])
                snprintf(region, sizeof region, "%s, %s", matches[i].region,
                         matches[i].country);
            else
                snprintf(region, sizeof region, "%s%s%s",
                         matches[i].region, matches[i].region[0] &&
                         matches[i].country[0] ? ", " : "", matches[i].country);
            row(r, "action", "savecity", matches[i].name, region, coord);
        }
    } else if (searched) {
        info(r, "No city found", "Check spelling or Wi-Fi and search again");
    }
}

static void handle(char **args, int count, Reply *r) {
    const char *operation = count > 1 ? args[1] : "";
    const char *arg = count > 2 ? args[2] : "";
    snprintf(r->status, sizeof r->status, "ok");
    if (!strcmp(operation, "menu"))
        page_menu(r);
    else if (!strcmp(operation, "location"))
        page_location(r, !strcmp(arg, "1"));
    else if (!strcmp(operation, "cities"))
        page_cities(r);
    else if (!strcmp(operation, "citysearch")) {
        start_search(arg);
        page_cities(r);
    } else if (!strcmp(operation, "savecity")) {
        double lat, lon;
        int slot = 1;
        char city[Q2_WEATHER_NAME] = {0};
        int found = 0;
        pthread_mutex_lock(&state_mutex);
        int fields = sscanf(arg, "%d %lf %lf", &slot, &lat, &lon);
        /* Old clients passed only the coordinates: retain compatibility. */
        if (fields != 3) {
            slot = 1;
            fields = sscanf(arg, "%lf %lf", &lat, &lon) == 2 ? 3 : 0;
        }
        if (!rt.search_pending && rt.search_count > 0 &&
            fields == 3 && (slot == 0 || slot == 1)) {
            for (int i = 0; i < rt.search_count; ++i) {
                WeatherGeoResult *item = &rt.search_matches[i];
                if (fabs(lat - item->latitude) < 0.000001 &&
                    fabs(lon - item->longitude) < 0.000001) {
                    if (item->region[0] && item->country[0])
                        snprintf(city, sizeof city, "%.26s, %.16s, %.16s",
                                 item->name, item->region, item->country);
                    else if (item->country[0])
                        snprintf(city, sizeof city, "%.42s, %.18s",
                                 item->name, item->country);
                    else
                        snprintf(city, sizeof city, "%s", item->name);
                    found = 1;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&state_mutex);
        if (!found) {
            snprintf(r->status, sizeof r->status, "error");
            snprintf(r->message, sizeof r->message, "Please search again");
        } else {
            pthread_mutex_lock(&state_mutex);
            int good = 1;
            if (slot == 0 && enable_manual_home()) good = 0;
            if (good) good = weather_set_location(&rt.app, slot, city, lat, lon) == WEATHER_OK;
            if (good) {
                rt.generation++;
                save_state();
            }
            pthread_mutex_unlock(&state_mutex);
            if (good) {
                refresh();
                snprintf(r->message, sizeof r->message,
                         slot ? "Saved city; updating weather" : "Home city; updating weather");
            } else {
                snprintf(r->status, sizeof r->status, "error");
                snprintf(r->message, sizeof r->message, "Invalid city");
            }
        }
    } else if (!strcmp(operation, "clear")) {
        pthread_mutex_lock(&state_mutex);
        memset(&rt.app.locations[1], 0, sizeof rt.app.locations[1]);
        rt.generation++;
        save_state();
        pthread_mutex_unlock(&state_mutex);
        snprintf(r->message, sizeof r->message, "Saved city removed");
    } else if (!strcmp(operation, "units")) {
        pthread_mutex_lock(&state_mutex);
        rt.units_f = !rt.units_f;
        pthread_mutex_unlock(&state_mutex);
        snprintf(r->message, sizeof r->message, "Temperature units changed");
    } else if (!strcmp(operation, "refresh")) {
        refresh();
        snprintf(r->message, sizeof r->message, "Updating both locations");
    } else {
        snprintf(r->status, sizeof r->status, "unsupported");
    }
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
    Reply reply = {0};
    struct timeval timeout = {5, 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    int n = 0;
    ssize_t got;
    while (n < WIRE_MAX &&
           (got = read(socket_fd, request + n, (size_t)(WIRE_MAX - n))) > 0)
        n += (int)got;
    request[n] = 0;
    char *fields[12] = {0};
    int fields_count = 0;
    if (!strncmp(request, "Q2SPOT/1\n", 9)) {
        char *line = request + 9;
        line[strcspn(line, "\n")] = 0;
        char *cursor = line;
        while (fields_count < 12 && cursor) {
            fields[fields_count++] = strsep(&cursor, "\t");
        }
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
    int hn = snprintf(header, sizeof header, "Q2SPOT/1\n%.20s\t%s\t%s\t\n", id,
                      reply.status, msg);
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
    weather_art_init();
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", SOCKET_PATH);
    unlink(SOCKET_PATH);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof addr) ||
        listen(fd, 4)) {
        if (fd >= 0) close(fd);
        return 1;
    }
    /* Let Q2 Pod finish launching before network access; Smart Wi-Fi sees
     * this app as running and wakes the radio on its next poll. */
    refresh();
    time_t last = time(NULL);
    while (!stopped && time(NULL) - last < 600) {
        /* Recover automatically after the Q2 reconnects. Poll infrequently
         * and never overlap active requests or bypass manual Wi-Fi Off. */
        time_t now = time(NULL);
        pthread_mutex_lock(&state_mutex);
        int pending = rt.pending;
        int failed = rt.error != WEATHER_OK || !rt.ever_refreshed;
        time_t elapsed = now - rt.last_attempt;
        pthread_mutex_unlock(&state_mutex);
        if (!pending && wifi_preference_on() &&
            elapsed >= (failed ? 60 : 900))
            refresh();
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 1000) > 0) {
            int accepted = accept(fd, NULL, NULL);
            if (accepted >= 0) {
                answer(accepted);
                last = time(NULL);
            }
        }
    }
    close(fd);
    unlink(SOCKET_PATH);
    weather_art_cleanup();
    return 0;
}
