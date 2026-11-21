/* SPDX-License-Identifier: MIT */
#include "http.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/* Link to Q2 Pod's own libcurl; values are from curl/curl.h. */
void *curl_easy_init(void);
int curl_easy_setopt(void *, int, ...);
int curl_easy_perform(void *);
int curl_easy_getinfo(void *, int, ...);
void curl_easy_cleanup(void *);

/* Diagnostics are deliberately short and contain no API credentials. */
static __thread char error_text[160];

const char *weather_http_error(void) { return error_text; }

void weather_log(const char *category, const char *detail) {
#ifndef HOST
    FILE *f = fopen("/mnt/mmc/.weather/weather.log", "ab");
#else
    FILE *f = fopen("build/weather.log", "ab");
#endif
    if (!f) return;
    time_t now = time(NULL);
    fprintf(f, "%ld %s: %.160s\n", (long)now, category, detail ? detail : "");
    fclose(f);
}

typedef struct {
    char *buf;
    size_t used;
    size_t capacity;
} Reply;

static size_t receive(char *data, size_t size, size_t count, void *user) {
    Reply *r = user;
    if (size && count > (r->capacity - r->used - 1) / size) return 0;
    size_t n = size * count;
    memcpy(r->buf + r->used, data, n);
    r->used += n;
    r->buf[r->used] = 0;
    return n;
}

int http_get(const char *url, char *body, size_t capacity) {
    if (!url || !body || capacity < 2) return -1;
    Reply r = { body, 0, capacity };
    body[0] = 0;
    void *curl = curl_easy_init();
    if (!curl) return -1;
    curl_easy_setopt(curl, 10002, url); /* CURLOPT_URL */
    curl_easy_setopt(
        curl, 10018,
        "Q2Weather/1.2 (+https://github.com/lampsonlnguyen/q2-weather)"); /* USERAGENT */
    curl_easy_setopt(curl, 20011, receive);                               /* WRITEFUNCTION */
    curl_easy_setopt(curl, 10001, &r);                                    /* WRITEDATA */
    curl_easy_setopt(curl, 78, 10L);                                      /* CONNECTTIMEOUT */
    curl_easy_setopt(curl, 13, 20L);                                      /* TIMEOUT */
    curl_easy_setopt(curl, 99, 1L);                                       /* NOSIGNAL */
    curl_easy_setopt(curl, 52, 1L);                                       /* FOLLOWLOCATION */
#ifndef HOST
    curl_easy_setopt(curl, 10065, "/etc/scrobble-ca.pem"); /* CAINFO */
#endif
    curl_easy_setopt(curl, 64, 1L); /* SSL_VERIFYPEER */
    curl_easy_setopt(curl, 81, 2L); /* SSL_VERIFYHOST */
    long status = 0;
    int result = curl_easy_perform(curl);
    if (!result) (void)curl_easy_getinfo(curl, 0x200002, &status);
    int ok = result == 0 && status == 200 && r.used;
    if (!ok) {
        const char *endpoint = strstr(url, "api.met.no")                    ? "met.no forecast"
                               : strstr(url, "nominatim.openstreetmap.org") ? "nominatim geocoding"
                               : strstr(url, "ipapi.co")                    ? "ipapi.co location"
                               : strstr(url, "api.open-meteo.com")          ? "open-meteo forecast"
                               : strstr(url, "geocoding-api.open-meteo.com")
                                   ? "open-meteo geocoding"
                                   : "ipwho.is";
        snprintf(error_text, sizeof error_text, "%s: curl=%d HTTP=%ld bytes=%zu", endpoint, result,
                 status, r.used);
        weather_log("HTTP", error_text);
    } else {
        error_text[0] = 0;
    }
    curl_easy_cleanup(curl);
    return ok ? 0 : -1;
}

/* Small JSON accessor for well-defined API responses. Strings with escaped
 * characters are not accepted as city labels, avoiding untrusted control text.
 * Search is bounded to the object/array key supplied by the API. */
static const char *key_at(const char *json, const char *key) {
    char token[80];
    if (snprintf(token, sizeof token, "\"%s\"", key) >= (int)sizeof token) return NULL;
    const char *p = json;
    while ((p = strstr(p, token))) {
        p += strlen(token);
        while (isspace((unsigned char)*p)) p++;
        if (*p++ == ':') {
            while (isspace((unsigned char)*p)) p++;
            return p;
        }
    }
    return NULL;
}

static int value_number(const char *json, const char *key, double *out) {
    const char *p = key_at(json, key);
    if (!p) return -1;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p || !isfinite(v) || (*end != ',' && *end != '}' && *end != ']')) return -1;
    *out = v;
    return 0;
}

static int value_string(const char *json, const char *key, char *out, size_t cap) {
    const char *p = key_at(json, key);
    if (!p || *p++ != '"' || cap < 2) return -1;
    size_t n = 0;
    while (*p && *p != '"') {
        if (*p == '\\' || (unsigned char)*p < 32 || n + 1 >= cap) return -1;
        out[n++] = *p++;
    }
    if (*p != '"' || !n) return -1;
    out[n] = 0;
    return 0;
}

static int number_array(const char *json, const char *key, int *out, int count) {
    const char *p = key_at(json, key);
    if (!p || *p++ != '[') return -1;
    for (int i = 0; i < count; i++) {
        while (isspace((unsigned char)*p)) p++;
        if (!strncmp(p, "null", 4)) {
            out[i] = -1;
            p += 4;
            while (isspace((unsigned char)*p)) p++;
            if (*p++ != (i + 1 == count ? ']' : ',')) return -1;
            continue;
        }
        char *end = NULL;
        double v = strtod(p, &end);
        if (end == p || !isfinite(v) || v < -200 || v > 1000) return -1;
        out[i] = (int)lround(v);
        p = end;
        while (isspace((unsigned char)*p)) p++;
        if (*p++ != (i + 1 == count ? ']' : ',')) return -1;
    }
    return 0;
}

static int date_array(const char *json, char dates[][11], int count) {
    const char *p = key_at(json, "time");
    if (!p || *p++ != '[') return -1;
    for (int i = 0; i < count; i++) {
        while (isspace((unsigned char)*p)) p++;
        if (*p++ != '"') return -1;
        for (int j = 0; j < 10; j++) {
            if (!p[j]) return -1;
            dates[i][j] = p[j];
        }
        dates[i][10] = 0;
        if (dates[i][4] != '-' || dates[i][7] != '-' || p[10] != '"') return -1;
        p += 11;
        while (isspace((unsigned char)*p)) p++;
        if (*p++ != (i + 1 == count ? ']' : ',')) return -1;
    }
    return 0;
}

/* IP location is optional. Provider failures cannot block forecasts for
 * already configured cities. A second independent provider is attempted. */
int weather_geolocate(char *city, size_t cap, double *lat, double *lon) {
    static const char *providers[] = { "https://ipwho.is/", "https://ipapi.co/json/" };
    char body[8192];
    if (!city || cap < 2 || !lat || !lon) return -1;
    for (size_t i = 0; i < 2; ++i) {
        city[0] = 0;
        if (http_get(providers[i], body, sizeof body)) continue;
        if (strstr(body, "\"success\":false") || value_string(body, "city", city, cap) ||
            value_number(body, "latitude", lat) || value_number(body, "longitude", lon) ||
            *lat < -90 || *lat > 90 || *lon < -180 || *lon > 180)
            continue;
        return 0;
    }
    weather_log("GEO", "Both IP location providers unavailable; choose Home City");
    return -1;
}

static int encode(const char *input, char *output, size_t capacity) {
    size_t n = 0;
    for (; *input; input++) {
        unsigned char c = (unsigned char)*input;
        if (isalnum(c) || c == '-' || c == '_' || c == '.') {
            if (n + 1 >= capacity) return -1;
            output[n++] = (char)c;
        } else if (c == ' ') {
            if (n + 1 >= capacity) return -1;
            output[n++] = '+';
        } else {
            if (n + 3 >= capacity) return -1;
            n += (size_t)snprintf(output + n, capacity - n, "%%%02X", c);
        }
    }
    if (!n) return -1;
    output[n] = 0;
    return 0;
}

/* Bound each result to its own JSON object: never mix names, countries or
 * coordinates from different matches. Open-Meteo's result objects are flat. */
int weather_geocode_parse(const char *json, WeatherGeoResult results[WEATHER_GEO_MAX]) {
    if (!json || !results) return -1;
    const char *p = key_at(json, "results");
    if (!p || *p != '[') return 0;
    ++p;
    int count = 0;
    while (*p && *p != ']' && count < WEATHER_GEO_MAX) {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == ',' || *p == '\t') ++p;
        if (*p == ']') break;
        if (*p != '{') return -1;
        const char *start = p++;
        int quoted = 0, escape_char = 0, depth = 1;
        while (*p && depth) {
            if (escape_char) {
                escape_char = 0;
                ++p;
                continue;
            }
            if (*p == '\\' && quoted) {
                escape_char = 1;
                ++p;
                continue;
            }
            if (*p == '"')
                quoted = !quoted;
            else if (!quoted && *p == '{')
                ++depth;
            else if (!quoted && *p == '}')
                --depth;
            ++p;
        }
        if (depth) return -1;
        size_t len = (size_t)(p - start);
        if (len >= 2048) return -1;
        char object[2048];
        memcpy(object, start, len);
        object[len] = 0;
        WeatherGeoResult item = { 0 };
        if (value_string(object, "name", item.name, sizeof item.name) ||
            value_number(object, "latitude", &item.latitude) ||
            value_number(object, "longitude", &item.longitude) || item.latitude < -90 ||
            item.latitude > 90 || item.longitude < -180 || item.longitude > 180)
            return -1;
        (void)value_string(object, "admin1", item.region, sizeof item.region);
        (void)value_string(object, "country", item.country, sizeof item.country);
        /* An empty country is valid in some API responses, but always
         * distinguish a match by coordinates when saving. */
        results[count++] = item;
    }
    return count;
}

/* Nominatim is an independent, rate-limited geocoder used only when
 * Open-Meteo's geocoding service is unavailable. Searches are user-initiated. */
static int fallback_geocode(const char *encoded, WeatherGeoResult results[WEATHER_GEO_MAX]) {
    char url[512], body[32768];
    snprintf(url, sizeof url,
             "https://nominatim.openstreetmap.org/search?q=%s&format=jsonv2&limit=8", encoded);
    if (http_get(url, body, sizeof body)) return -1;
    const char *p = body;
    int count = 0;
    while ((p = strchr(p, '{')) && count < WEATHER_GEO_MAX) {
        const char *end = strchr(p, '}');
        if (!end || end - p >= 4096) return -1;
        char object[4096], display[1024], lat_text[40], lon_text[40];
        size_t length = (size_t)(end - p + 1);
        memcpy(object, p, length);
        object[length] = 0;
        p = end + 1;
        if (value_string(object, "display_name", display, sizeof display) ||
            value_string(object, "lat", lat_text, sizeof lat_text) ||
            value_string(object, "lon", lon_text, sizeof lon_text))
            continue;
        char *last_lat = NULL, *last_lon = NULL;
        double lat = strtod(lat_text, &last_lat);
        double lon = strtod(lon_text, &last_lon);
        if (!last_lat || *last_lat || !last_lon || *last_lon || !isfinite(lat) || !isfinite(lon) ||
            lat < -90 || lat > 90 || lon < -180 || lon > 180)
            continue;
        WeatherGeoResult *item = &results[count++];
        size_t name_length = strcspn(display, ",");
        if (name_length >= sizeof item->name) name_length = sizeof item->name - 1;
        memcpy(item->name, display, name_length);
        item->name[name_length] = 0;
        snprintf(item->region, sizeof item->region, "%.63s", display);
        item->latitude = lat;
        item->longitude = lon;
    }
    if (count > 0) weather_log("GEO", "Using OSM Nominatim city search fallback");
    return count;
}

int weather_geocode_many(const char *query, WeatherGeoResult results[WEATHER_GEO_MAX]) {
    char encoded[256], url[512], body[32768];
    if (encode(query, encoded, sizeof encoded)) return -1;
    snprintf(
        url, sizeof url,
        "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=8&language=en&format=json",
        encoded);
    if (!http_get(url, body, sizeof body)) {
        int count = weather_geocode_parse(body, results);
        if (count >= 0) return count;
    }
    return fallback_geocode(encoded, results);
}

int weather_geocode(const char *query, char *city, size_t cap, double *lat, double *lon) {
    WeatherGeoResult results[WEATHER_GEO_MAX] = { 0 };
    int n = weather_geocode_many(query, results);
    if (n <= 0 || !city || !lat || !lon || !cap) return -1;
    if (snprintf(city, cap, "%s", results[0].name) >= (int)cap) return -1;
    *lat = results[0].latitude;
    *lon = results[0].longitude;
    return 0;
}

static int weather_open_meteo_request(double lat, double lon, int *temp, int *humidity, int *code,
                                      long *updated, char dates[][11], int highs[], int lows[],
                                      int precip[], int codes[], int day_count) {
    if (day_count != 7 || lat < -90 || lat > 90 || lon < -180 || lon > 180) return -1;
    char url[1024], body[16384];
    snprintf(url, sizeof url,
             "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
             "&current=temperature_2m,relative_humidity_2m,weather_code"
             "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
             "weather_code&forecast_days=7&timezone=auto",
             lat, lon);
    if (http_get(url, body, sizeof body)) return -1;
    const char *current = key_at(body, "current");
    const char *daily = key_at(body, "daily");
    if (!current || *current != '{') {
        weather_log("PARSE", "Open-Meteo response missing current object");
        return -1;
    }
    double t, h, w;
    if (value_number(current, "temperature_2m", &t) ||
        value_number(current, "relative_humidity_2m", &h) ||
        value_number(current, "weather_code", &w)) {
        weather_log("PARSE", "Open-Meteo current temperature/humidity/weather_code invalid");
        return -1;
    }
    *temp = (int)lround(t);
    *humidity = (int)lround(h);
    *code = (int)lround(w);
    *updated = (long)time(NULL);
    if (!daily || *daily != '{' || date_array(daily, dates, day_count) ||
        number_array(daily, "temperature_2m_max", highs, day_count) ||
        number_array(daily, "temperature_2m_min", lows, day_count) ||
        number_array(daily, "weather_code", codes, day_count)) {
        weather_log("PARSE", "Open-Meteo daily forecast missing or invalid");
        return 1; /* Current readings are still usable. */
    }
    if (number_array(daily, "precipitation_probability_max", precip, day_count)) {
        weather_log("PARSE", "Open-Meteo precipitation probabilities unavailable");
        for (int i = 0; i < day_count; i++) precip[i] = -1;
    }
    return 0;
}

/* MET Norway Locationforecast 2.0: independently hosted global forecast.
 * Daily values are aggregated from hourly/6-hourly UTC samples because this
 * API supplies a point time series rather than Open-Meteo-style daily arrays.
 * Missing precipitation probability remains -1; never invent a percentage. */
static int met_symbol_code(const char *symbol) {
    if (strstr(symbol, "thunder")) return 95;
    if (strstr(symbol, "snow")) return 71;
    if (strstr(symbol, "sleet")) return 67;
    if (strstr(symbol, "rain")) return 61;
    if (strstr(symbol, "fog")) return 45;
    if (strstr(symbol, "cloudy")) return strstr(symbol, "partly") ? 2 : 3;
    if (strstr(symbol, "fair")) return 1;
    if (strstr(symbol, "clearsky")) return 0;
    return 3;
}

static int weather_met_request(double lat, double lon, int *temp, int *humidity, int *code,
                               long *updated, char dates[][11], int highs[], int lows[],
                               int precip[], int codes[], int count) {
    /* The compact JSON commonly occupies tens of KB. Keep headroom without
     * allocating large persistent data structures on this embedded player. */
    char url[256];
    static char body[196608];
    snprintf(url, sizeof url,
             "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=%.4f&lon=%.4f", lat,
             lon);
    if (http_get(url, body, sizeof body)) return -1;
    const char *start = key_at(body, "timeseries");
    if (!start || *start != '[') return -1;
    for (int i = 0; i < count; ++i) {
        dates[i][0] = 0;
        highs[i] = -200;
        lows[i] = 200;
        precip[i] = -1;
        codes[i] = 3;
    }
    int day_count = 0, first = 1;
    const char *p = start + 1;
    while ((p = strstr(p, "\"time\"")) && day_count <= count) {
        const char *next = strstr(p + 6, "\"time\"");
        const char *end = next ? next : p + strlen(p);
        size_t length = (size_t)(end - p);
        if (length > 4095) length = 4095;
        char item[4096];
        memcpy(item, p, length);
        item[length] = 0;
        char iso[40], symbol[96];
        double t, h, clouds;
        if (value_string(item, "time", iso, sizeof iso) || strlen(iso) < 10 ||
            value_number(item, "air_temperature", &t)) {
            if (!next) break;
            p = next;
            continue;
        }
        if (first) {
            if (value_number(item, "relative_humidity", &h)) h = 0;
            if (value_number(item, "cloud_area_fraction", &clouds)) clouds = 100;
            *temp = (int)lround(t);
            *humidity = (int)lround(h);
            *code = clouds < 10 ? 0 : clouds < 60 ? 2 : 3;
            if (!value_string(item, "symbol_code", symbol, sizeof symbol))
                *code = met_symbol_code(symbol);
            *updated = (long)time(NULL);
            first = 0;
        }
        int day = -1;
        for (int i = 0; i < day_count; ++i)
            if (!strncmp(dates[i], iso, 10)) {
                day = i;
                break;
            }
        if (day < 0) {
            if (day_count == count) break;
            day = day_count++;
            memcpy(dates[day], iso, 10);
            dates[day][10] = 0;
        }
        int rounded = (int)lround(t);
        if (rounded > highs[day]) highs[day] = rounded;
        if (rounded < lows[day]) lows[day] = rounded;
        double probability;
        if (!value_number(item, "probability_of_precipitation", &probability) && probability >= 0 &&
            probability <= 100) {
            int chance = (int)lround(probability);
            if (chance > precip[day]) precip[day] = chance;
        }
        if (!value_string(item, "symbol_code", symbol, sizeof symbol))
            codes[day] = met_symbol_code(symbol);
        if (!next) break;
        p = next;
    }
    if (first) return -1;
    if (day_count < count) return 1; /* Keep current data if daily series is short. */
    weather_log("FORECAST", "Open-Meteo unavailable; using MET Norway UTC forecast");
    return 0;
}

int weather_request(double lat, double lon, int *temp, int *humidity, int *code, long *updated,
                    char dates[][11], int highs[], int lows[], int precip[], int codes[],
                    int day_count) {
    if (day_count != 7 || !isfinite(lat) || !isfinite(lon) || lat < -90 || lat > 90 || lon < -180 ||
        lon > 180)
        return -1;
    int result = weather_open_meteo_request(lat, lon, temp, humidity, code, updated, dates, highs,
                                            lows, precip, codes, day_count);
    if (result == 0) return 0;
    /* Even a partial Open-Meteo response can lack the 7-day series.
     * Prefer an independent full forecast, retaining current data if it fails. */
    int alternate = weather_met_request(lat, lon, temp, humidity, code, updated, dates, highs,
                                        lows, precip, codes, day_count);
    return alternate >= 0 ? alternate : result;
}

const char *weather_description(int code) {
    if (code == 0) return "Clear";
    if (code <= 3) return "Partly cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code >= 85 && code <= 86) return "Snow showers";
    if (code >= 95) return "Thunderstorms";
    return "Cloudy";
}
