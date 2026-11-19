/* SPDX-License-Identifier: MIT */
#include "../src/lang.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    assert(!strcmp(weather_text(WX_WEATHER), "Weather"));
    const char pack[] = "WEATHER=Forecasts\nLOCATIONS=Cities\n"
                        "BAD=Ignored\nBACK=\n";
    weather_language_parse(pack, sizeof pack - 1);
    assert(!strcmp(weather_text(WX_WEATHER), "Forecasts"));
    assert(!strcmp(weather_text(WX_LOCATIONS), "Cities"));
    assert(!strcmp(weather_text(WX_BACK), "Back"));
    const char windows[] = "WEATHER=Weather from Windows\r\n";
    weather_language_parse(windows, sizeof windows - 1);
    assert(!strcmp(weather_text(WX_WEATHER), "Weather from Windows"));
    weather_language_parse(NULL, 0);
    assert(!strcmp(weather_text(WX_WEATHER), "Weather"));
    puts("Q2 Weather language pack override / English fallback passed");
    return 0;
}
