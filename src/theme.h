/* SPDX-License-Identifier: MIT */
#ifndef WEATHER_THEME_H
#define WEATHER_THEME_H
#include <stddef.h>

typedef struct {
    int style;       /* 0 Minimal, 1 Classic, 2 Modern */
    int light;       /* Effective light appearance, including automatic mode */
    int accent;      /* 0–8; 9 for Classic Blue */
    int wheel;       /* Q2POD/WHEELSENSITIVITY, percentage */
    int language;    /* Stock language identifier when exposed, else 0 (English) */
} WeatherTheme;

/* Read a bounded Q2 Pod config snapshot; no global state or settings writes. */
void weather_theme_parse(WeatherTheme *out, const char *data, size_t len, int hour);
#endif
