/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "http.h"
#include "weather.h"

#include <errno.h>
#include <poll.h>
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
#define WIRE_MAX 8192
#define ROW_MAX 32768

typedef struct {
    WeatherApp app;
    int state;
    int pending;
    int error;
    int units_f;
    char search[80];
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

static int wait_online(void *unused, int timeout_seconds) {
    (void)unused;
    for (int i = 0; i < timeout_seconds && !stopped; i += 2) {
        char body[4096];
        if (!http_get("https://ipwho.is/", body, sizeof body)) return 0;
        sleep(2);
    }
    return -1;
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
    if (weather_request(lat, lon, temp, humidity, code, updated, dates, highs, lows, rain,
                        codes, Q2_FORECAST_DAYS))
        return -1;
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

static void refresh(void) {
    rt.pending = 1;
    rt.error = weather_on_open(&rt.app, &backend);
    rt.pending = 0;
    save_state();
}

static void display_number(char *out, size_t cap, int celsius) {
    int value = rt.units_f ? (celsius * 9 + (celsius >= 0 ? 2 : -2)) / 5 + 32 : celsius;
    snprintf(out, cap, "%d%c", value, rt.units_f ? 'F' : 'C');
}

static void page_location(Reply *r, int slot) {
    WeatherLocation *loc = &rt.app.locations[slot];
    char temp[32], subtitle[128], date[64], hi[32], lo[32];
    if (!loc->used) {
        info(r, slot ? "No saved city" : "Current city unavailable",
             slot ? "Choose a city below" : "Refresh to detect your city");
    } else {
        if (loc->valid) {
            display_number(temp, sizeof temp, loc->temperature_c);
            snprintf(subtitle, sizeof subtitle, "%s | Humidity %d%%",
                     weather_description(loc->weather_code), loc->humidity);
            info(r, temp, subtitle);
        } else {
            info(r, loc->name, "No weather downloaded yet");
        }
        if (loc->forecast_valid) {
            for (int i = 0; i < Q2_FORECAST_DAYS; i++) {
                WeatherDay *d = &loc->days[i];
                display_number(hi, sizeof hi, d->high_c);
                display_number(lo, sizeof lo, d->low_c);
                snprintf(date, sizeof date, "%.10s", d->date);
                snprintf(subtitle, sizeof subtitle, "%s / %s | Rain %d%% | %s", hi, lo,
                         d->precipitation_pct, weather_description(d->weather_code));
                info(r, date, subtitle);
            }
        }
        if (loc->valid) info(r, "Last updated", "Latest available reading; may be cached");
    }
    if (slot) {
        row(r, "list", "cities", "Change saved city", "Search for a city", "");
        row(r, "action", "clear", "Remove saved city", "", "");
    }
    row(r, "action", "refresh", "Refresh both locations", "Fetch current and 7-day weather", "");
}

static void page_menu(Reply *r) {
    char sub[128];
    for (int i = 0; i < Q2_WEATHER_SLOTS; i++) {
        WeatherLocation *l = &rt.app.locations[i];
        if (l->used && l->valid) {
            char temp[24];
            display_number(temp, sizeof temp, l->temperature_c);
            snprintf(sub, sizeof sub, "%s | %s | %s", l->name, temp,
                     weather_description(l->weather_code));
        } else {
            snprintf(sub, sizeof sub, "%s", l->used ? l->name : "Not yet configured");
        }
        row(r, "list", "location", i ? "Saved Location" : "Current Location", sub,
            i ? "1" : "0");
    }
    row(r, "action", "refresh", "Refresh weather", "Update both locations", "");
    row(r, "list", "cities", "Choose saved city", "Enter a city name", "");
    row(r, "action", "units", rt.units_f ? "Use Celsius" : "Use Fahrenheit",
        "Switch displayed temperature units", "");
    if (!wifi_preference_on())
        info(r, "Wi-Fi is off", "Enable Wi-Fi in Q2 settings to refresh");
    else if (rt.error)
        info(r, "Weather may be cached", "Refresh was unsuccessful");
}

static void page_cities(Reply *r) {
    row(r, "field", "citysearch", "City name:", "Enter city to search", "");
    if (rt.search[0]) {
        char name[Q2_WEATHER_NAME];
        double lat, lon;
        if (weather_geocode(rt.search, name, sizeof name, &lat, &lon) == 0) {
            char coord[80];
            snprintf(coord, sizeof coord, "%.6f %.6f", lat, lon);
            row(r, "action", "savecity", name, "Save this city", coord);
        } else {
            info(r, "City not found", "Check spelling and Wi-Fi");
        }
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
        snprintf(rt.search, sizeof rt.search, "%s", arg);
        page_cities(r);
    } else if (!strcmp(operation, "savecity")) {
        double lat, lon;
        if (sscanf(arg, "%lf %lf", &lat, &lon) != 2 ||
            weather_set_location(&rt.app, 1, rt.search, lat, lon)) {
            snprintf(r->status, sizeof r->status, "error");
            snprintf(r->message, sizeof r->message, "Invalid city");
        } else {
            char city[Q2_WEATHER_NAME];
            double checked_lat, checked_lon;
            if (!weather_geocode(rt.search, city, sizeof city, &checked_lat, &checked_lon) &&
                weather_set_location(&rt.app, 1, city, checked_lat, checked_lon) == 0) {
                save_state();
                refresh();
                snprintf(r->message, sizeof r->message, "Saved location updated");
            } else {
                snprintf(r->status, sizeof r->status, "error");
                snprintf(r->message, sizeof r->message, "City lookup failed");
            }
        }
    } else if (!strcmp(operation, "clear")) {
        memset(&rt.app.locations[1], 0, sizeof rt.app.locations[1]);
        save_state();
        snprintf(r->message, sizeof r->message, "Saved city removed");
    } else if (!strcmp(operation, "units")) {
        rt.units_f = !rt.units_f;
        snprintf(r->message, sizeof r->message, "Temperature units changed");
    } else if (!strcmp(operation, "refresh")) {
        refresh();
        snprintf(r->message, sizeof r->message, rt.error ? "Refresh failed; cached weather kept"
                                                       : "Weather updated");
    } else {
        snprintf(r->status, sizeof r->status, "unsupported");
    }
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
        (void)write(socket_fd, header, (size_t)hn);
        if (reply.used) (void)write(socket_fd, reply.rows, reply.used);
    }
    close(socket_fd);
}

int main(void) {
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGPIPE, SIG_IGN);
    weather_init(&rt.app);
    load_state();
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
    return 0;
}
