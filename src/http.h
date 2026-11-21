/* SPDX-License-Identifier: MIT */
#ifndef Q2_HTTP_H
#define Q2_HTTP_H
#include <stddef.h>
int http_get(const char *url, char *body, size_t capacity);
const char *weather_http_error(void);
void weather_log(const char *category, const char *detail);
int weather_geolocate(char *city, size_t capacity, double *latitude, double *longitude);
#define WEATHER_GEO_MAX 8
typedef struct {
    char name[64];
    char region[64];
    char country[64];
    double latitude, longitude;
} WeatherGeoResult;
int weather_geocode_many(const char *query, WeatherGeoResult results[WEATHER_GEO_MAX]);
int weather_geocode_parse(const char *json, WeatherGeoResult results[WEATHER_GEO_MAX]);
int weather_geocode(const char *query, char *city, size_t capacity, double *latitude,
                    double *longitude);
int weather_request(double latitude, double longitude, int *temperature, int *humidity,
                    int *weather_code, long *updated, char dates[][11], int highs[], int lows[],
                    int precip[], int codes[], int day_count);
const char *weather_description(int code);
#endif
