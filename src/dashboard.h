/* SPDX-License-Identifier: MIT */
#ifndef Q2_WEATHER_DASHBOARD_H
#define Q2_WEATHER_DASHBOARD_H
#include "weather.h"
#include <stdint.h>
#define DASH_WIDTH 375
#define DASH_HEIGHT 320
#define WEATHER_CITY_RESULTS_MAX 8
typedef struct {
    char name[64];
    char region[128];
    double latitude, longitude;
} WeatherCityChoice;
typedef struct {
    WeatherApp weather;
    int slot;
    int day;
    int units_f;
    int theme_light;
    int theme_id;
    int theme_accent;
    int wheel_percent;
    int language_id;
    int ready;
    int detailed;
    int redraw;
    int quit;
    int wheel_rem;
    int page; /* 0 locations, 1 forecast dashboard, 2 settings, 3 city entry, 4 day detail, 5 city results */
    int keyboard_index;
    int keyboard_numbers; /* 0 ABC, 1 123/symbols */
    int home_selected; /* Current, Saved, Settings */
    int city_target; /* 0 Current/Home, 1 Saved */
    int search_request;
    int refresh_request;
    int clear_request;
    int search_result; /* 0 idle, 1 searching, 2 saved, -1 not found */
    char city_query[48];
    int city_length;
    int native_keyboard;
    WeatherCityChoice matches[WEATHER_CITY_RESULTS_MAX];
    int match_count;
    int match_selected;
    int save_request;
} Dashboard;
void dashboard_init(Dashboard *ui);
int dashboard_input(Dashboard *ui, const char *event);
void dashboard_render(const Dashboard *ui, uint32_t *pixels, unsigned stride);
#endif
