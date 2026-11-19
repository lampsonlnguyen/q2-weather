/* SPDX-License-Identifier: MIT */
#ifndef Q2_WEATHER_ART_H
#define Q2_WEATHER_ART_H

/* Uses Q2 Pod's native, theme-aware JPEG art rows, not /dev/fb0. */
const char *weather_art_icon(int wmo);
unsigned weather_art_key(const char *uri, const char *context);
void weather_art_init(void);
void weather_art_row(const char *uri, const char *context, int wmo);
void weather_art_cleanup(void);
#endif
