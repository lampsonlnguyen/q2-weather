/* SPDX-License-Identifier: MIT */
#ifndef Q2_HTTP_H
#define Q2_HTTP_H
#include <stddef.h>
int http_get(const char *url, char *body, size_t capacity);
const char *weather_http_error(void);
void weather_log(const char *category, const char *detail);
int weather_geolocate(char *city, size_t capacity, double *latitude, double *longitude);
int weather_geocode(const char *query, char *city, size_t capacity, double *latitude,
                    double *longitude);
int weather_request(double latitude, double longitude, int *temperature, int *humidity,
                    int *weather_code, long *updated, char dates[][11], int highs[],
                    int lows[], int precip[], int codes[], int day_count);
const char *weather_description(int code);
#endif
