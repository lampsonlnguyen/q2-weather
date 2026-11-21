/* SPDX-License-Identifier: MIT */
#include "../src/art.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    assert(!strcmp(weather_art_icon(0), "sunny"));
    assert(!strcmp(weather_art_icon(1), "partly"));
    assert(!strcmp(weather_art_icon(2), "partly"));
    assert(!strcmp(weather_art_icon(3), "overcast"));
    assert(!strcmp(weather_art_icon(45), "fog"));
    assert(!strcmp(weather_art_icon(51), "drizzle"));
    assert(!strcmp(weather_art_icon(63), "rain"));
    assert(!strcmp(weather_art_icon(80), "showers"));
    assert(!strcmp(weather_art_icon(71), "snow"));
    assert(!strcmp(weather_art_icon(85), "snowshowers"));
    assert(!strcmp(weather_art_icon(95), "thunder"));
    assert(!strcmp(weather_art_icon(-1), "cloudy"));
    assert(!strcmp(weather_condition(1), "Mostly clear"));
    assert(!strcmp(weather_condition(3), "Overcast"));
    assert(weather_art_key("location", "0") != weather_art_key("location", "1"));
    assert(weather_art_key("forecast", "0-0") != weather_art_key("forecast", "0-1"));
    assert(weather_art_key("", "") == 0x0c0c6824u);
    weather_art_init();
    weather_art_row("location", "0", 0);
    weather_art_cleanup();
    puts("weather icon mappings and art keys passed");
    return 0;
}
