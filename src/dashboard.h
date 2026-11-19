/* SPDX-License-Identifier: MIT */
#ifndef Q2_WEATHER_DASHBOARD_H
#define Q2_WEATHER_DASHBOARD_H
#include "weather.h"
#include <stdint.h>
#define DASH_WIDTH 375
#define DASH_HEIGHT 320
typedef struct {
    WeatherApp weather;
    int slot;
    int day;
    int units_f;
    int ready;
    int detailed;
    int redraw;
    int quit;
    int wheel_rem;
} Dashboard;
void dashboard_init(Dashboard *ui);
int dashboard_input(Dashboard *ui, const char *event);
void dashboard_render(const Dashboard *ui, uint32_t *pixels, unsigned stride);
#endif
