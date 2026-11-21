/* SPDX-License-Identifier: MIT */
#ifndef Q2_WEATHER_H
#define Q2_WEATHER_H
#include <stddef.h>
#define Q2_WEATHER_SLOTS 2
#define Q2_FORECAST_DAYS 7
/* Dates use ISO-8601 YYYY-MM-DD in the destination's local timezone. */
typedef struct {
    char date[11];
    int high_c, low_c, precipitation_pct, weather_code;
} WeatherDay;
/* Slot 0: network current location; slot 1: user-saved city. */
#define Q2_WEATHER_NAME 64
typedef struct {
    char name[Q2_WEATHER_NAME];
    double latitude, longitude;
    int used, valid, temperature_c, humidity, weather_code;
    long updated_at;
    int forecast_valid;
    WeatherDay days[Q2_FORECAST_DAYS];
} WeatherLocation;
typedef struct {
    WeatherLocation locations[Q2_WEATHER_SLOTS];
} WeatherApp;
typedef struct {
    void *ctx;
    /* Radio state: 0 off, 1 on, negative error. */
    int (*wifi_state)(void *);
    int (*set_wifi)(void *, int);
    int (*wait_online)(void *, int seconds);
    int (*detect_location)(void *, char *name, size_t cap, double *lat, double *lon);
    int (*get_weather)(void *, double lat, double lon, int *temp, int *humidity, int *code,
                       long *timestamp);
    /* One HTTPS request can return both current and daily forecast values. */
    int (*get_forecast)(void *, double lat, double lon, WeatherDay *days, int count);
} WeatherBackend;
enum {
    WEATHER_OK = 0,
    WEATHER_INVALID = -1,
    WEATHER_RADIO = -2,
    WEATHER_OFFLINE = -3,
    WEATHER_GEO = -4,
    WEATHER_FETCH = -5
};
void weather_init(WeatherApp *);
int weather_set_location(WeatherApp *, int slot, const char *name, double lat, double lon);
int weather_refresh(WeatherApp *, const WeatherBackend *, int detect_current);
/* Call once on each application launch; always detects current location and refreshes both. */
int weather_on_open(WeatherApp *, const WeatherBackend *);
int weather_write(const WeatherApp *, char *, size_t);
int weather_read(WeatherApp *, const char *);
#endif
