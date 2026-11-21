/* SPDX-License-Identifier: MIT */
#include "../src/theme.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static WeatherTheme parse(const char *s, int hour) {
    WeatherTheme t;
    weather_theme_parse(&t, s, strlen(s), hour);
    return t;
}

int main(void) {
    WeatherTheme t = parse("", 12);
    assert(t.style == 0 && t.light == 0 && t.accent == 0 && t.wheel == 50);
    t = parse("[IPOD]\nTHEME=1\nLIGHT=1\nCLASSIC_LIGHT_ACCENT=9\n", 18);
    assert(t.style == 1 && t.light == 1 && t.accent == 9);
    t = parse("[IPOD]\nTHEME=2\nLIGHT=2\nACCENT=4\n"
              "[Q2POD]\nWHEELSENSITIVITY=130\n"
              "[SYSSET]\nLANGUAGE=2\n",
              13);
    assert(t.style == 2 && t.light == 1 && t.accent == 4);
    assert(t.wheel == 130 && t.language == 2);
    t = parse("[IPOD]\nTHEME=2\nLIGHT=2\nACCENT=7\n", 22);
    assert(t.style == 2 && !t.light && t.accent == 7);
    t = parse("[IPOD]\nTHEME=0\nMINIMAL_ACCENT=8\n"
              "[Q2POD]\nWHEELSENSITIVITY=999\n",
              8);
    assert(t.accent == 8 && t.wheel == 50);
    t = parse("[IPOD]\nTHEME=1\nCLASSIC_LIGHT_ACCENT=10\n", 9);
    assert(t.accent == 9);
    puts("Q2 system theme, auto light, accent, locale and wheel tests passed");
    return 0;
}
