/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "http.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Link to Q2 Pod's own libcurl; values are from curl/curl.h. */
void *curl_easy_init(void);
int curl_easy_setopt(void *, int, ...);
int curl_easy_perform(void *);
int curl_easy_getinfo(void *, int, ...);
void curl_easy_cleanup(void *);

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
    Reply r = {body, 0, capacity};
    body[0] = 0;
    void *curl = curl_easy_init();
    if (!curl) return -1;
    curl_easy_setopt(curl, 10002, url); /* CURLOPT_URL */
    curl_easy_setopt(curl, 10018, "Q2 Weather/0.1"); /* USERAGENT */
    curl_easy_setopt(curl, 20011, receive); /* WRITEFUNCTION */
    curl_easy_setopt(curl, 10001, &r); /* WRITEDATA */
    curl_easy_setopt(curl, 78, 10L); /* CONNECTTIMEOUT */
    curl_easy_setopt(curl, 13, 20L); /* TIMEOUT */
    curl_easy_setopt(curl, 99, 1L); /* NOSIGNAL */
    curl_easy_setopt(curl, 52, 1L); /* FOLLOWLOCATION */
#ifndef HOST
    curl_easy_setopt(curl, 10065, "/etc/scrobble-ca.pem"); /* CAINFO */
#endif
    curl_easy_setopt(curl, 64, 1L); /* SSL_VERIFYPEER */
    curl_easy_setopt(curl, 81, 2L); /* SSL_VERIFYHOST */
    long status = 0;
    int ok = !curl_easy_perform(curl) && !curl_easy_getinfo(curl, 0x200002, &status) &&
             status == 200 && r.used;
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

int weather_geolocate(char *city, size_t cap, double *lat, double *lon) {
    char body[8192];
    if (http_get("https://ipwho.is/", body, sizeof body) ||
        strstr(body, "\"success\":false") ||
        value_string(body, "city", city, cap) ||
        value_number(body, "latitude", lat) || value_number(body, "longitude", lon))
        return -1;
    return *lat >= -90 && *lat <= 90 && *lon >= -180 && *lon <= 180 ? 0 : -1;
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

int weather_geocode(const char *query, char *city, size_t cap, double *lat, double *lon) {
    char encoded[256], url[512], body[16384];
    if (encode(query, encoded, sizeof encoded)) return -1;
    snprintf(url, sizeof url,
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json",
             encoded);
    if (http_get(url, body, sizeof body) || !strstr(body, "\"results\"") ||
        value_string(body, "name", city, cap) || value_number(body, "latitude", lat) ||
        value_number(body, "longitude", lon))
        return -1;
    return *lat >= -90 && *lat <= 90 && *lon >= -180 && *lon <= 180 ? 0 : -1;
}

int weather_request(double lat, double lon, int *temp, int *humidity, int *code, long *updated,
                    char dates[][11], int highs[], int lows[], int precip[], int codes[],
                    int day_count) {
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
    if (!current || !daily || *current != '{' || *daily != '{' || current >= daily) return -1;
    double t, h, w;
    if (value_number(current, "temperature_2m", &t) ||
        value_number(current, "relative_humidity_2m", &h) ||
        value_number(current, "weather_code", &w) ||
        date_array(daily, dates, day_count) ||
        number_array(daily, "temperature_2m_max", highs, day_count) ||
        number_array(daily, "temperature_2m_min", lows, day_count) ||
        number_array(daily, "precipitation_probability_max", precip, day_count) ||
        number_array(daily, "weather_code", codes, day_count))
        return -1;
    *temp = (int)lround(t);
    *humidity = (int)lround(h);
    *code = (int)lround(w);
    *updated = (long)time(NULL);
    return 0;
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
