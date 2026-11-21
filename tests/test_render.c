/* SPDX-License-Identifier: MIT */
#include "../src/dashboard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Host render test: every page x every theme (Minimal/Classic/Modern) x
 * light/dark, dumped as PPM to build/renders/ for visual checking.
 * The framebuffer is rotated (dashboard_render writes
 * dst[x * stride + DASH_HEIGHT - y - 1]), so PPM pixel (x, y) reads that
 * same slot back. Run `make check`, then convert with e.g.
 * python3 -c "from PIL import Image; ..." or any PPM viewer. */
static void write_ppm(const char *path, const uint32_t *px, unsigned stride) {
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", DASH_WIDTH, DASH_HEIGHT);
    for (int y = 0; y < DASH_HEIGHT; y++) {
        for (int x = 0; x < DASH_WIDTH; x++) {
            uint32_t c = px[(unsigned)x * stride + DASH_HEIGHT - y - 1];
            unsigned char rgb[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8),
                                     (unsigned char)c };
            assert(fwrite(rgb, 1, 3, f) == 3);
        }
    }
    fclose(f);
}

static void fixture(Dashboard *ui) {
    dashboard_init(ui);
    ui->units_f = 0;
    WeatherLocation *cur = &ui->weather.locations[0];
    cur->used = 1;
    cur->valid = 1;
    snprintf(cur->name, sizeof cur->name, "Westminster");
    cur->temperature_c = 22;
    cur->humidity = 45;
    cur->weather_code = 1;
    cur->forecast_valid = 1;
    static const char *dates[7] = { "2026-11-21", "2026-11-22", "2026-11-23", "2026-11-24",
                                    "2026-11-25", "2026-11-26", "2026-11-27" };
    static const int codes[7] = { 1, 3, 61, 0, 80, 71, 95 };
    static const int highs[7] = { 24, 23, 21, 25, 22, 19, 20 };
    static const int lows[7] = { 14, 13, 12, 15, 13, 10, 11 };
    for (int i = 0; i < 7; i++) {
        snprintf(cur->days[i].date, sizeof cur->days[i].date, "%s", dates[i]);
        cur->days[i].weather_code = codes[i];
        cur->days[i].high_c = highs[i];
        cur->days[i].low_c = lows[i];
        cur->days[i].precipitation_pct = 10 * i;
    }
    WeatherLocation *saved = &ui->weather.locations[1];
    saved->used = 1;
    saved->valid = 1;
    snprintf(saved->name, sizeof saved->name, "Ho Chi Minh City");
    saved->temperature_c = 31;
    saved->humidity = 78;
    saved->weather_code = 3;
    saved->forecast_valid = 1;
    memcpy(saved->days, cur->days, sizeof cur->days);
    ui->match_count = 3;
    snprintf(ui->matches[0].name, sizeof ui->matches[0].name, "Westminster");
    snprintf(ui->matches[0].region, sizeof ui->matches[0].region, "California, USA");
    snprintf(ui->matches[1].name, sizeof ui->matches[1].name, "Westminster");
    snprintf(ui->matches[1].region, sizeof ui->matches[1].region, "Colorado, USA");
    snprintf(ui->matches[2].name, sizeof ui->matches[2].name, "Westminster");
    snprintf(ui->matches[2].region, sizeof ui->matches[2].region, "London, UK");
    snprintf(ui->city_query, sizeof ui->city_query, "Westm");
    ui->city_length = 5;
}

int main(void) {
    static const char *themes[3] = { "minimal", "classic", "modern" };
    static const char *pages[6] = { "locations",  "forecast",   "settings",
                                    "city-entry", "day-detail", "city-results" };
    /* page -> Dashboard.page value */
    static const int page_id[6] = { 0, 1, 2, 3, 4, 5 };
    static uint32_t px[DASH_WIDTH * DASH_HEIGHT];
    char path[128];
    for (int t = 0; t < 3; t++) {
        for (int light = 0; light < 2; light++) {
            for (int p = 0; p < 6; p++) {
                Dashboard ui;
                fixture(&ui);
                ui.theme_id = t;
                ui.theme_accent = 4; /* Cobalt, so the accent is visible */
                ui.theme_light = light;
                ui.page = page_id[p];
                ui.slot = 0;
                ui.day = 2;
                ui.home_selected = 1;
                ui.keyboard_index = p == 2 ? 1 : p == 3 ? 5 : 0;
                ui.match_selected = 1;
                memset(px, 0, sizeof px);
                dashboard_render(&ui, px, DASH_HEIGHT);
                snprintf(path, sizeof path, "build/renders/%s-%s-%s.ppm", pages[p], themes[t],
                         light ? "light" : "dark");
                write_ppm(path, px, DASH_HEIGHT);
            }
        }
    }
    puts("Rendered 6 pages x 3 themes x light/dark to build/renders/");
    return 0;
}
