/* SPDX-License-Identifier: MIT */
#include "dashboard.h"
#include "art.h"

#include <stdio.h>
#include <string.h>

/* Generated 24x24 pixels and an 8x12 bitmap font. No runtime decoding. */
#include "weather_sprites.inc"

#define BG 0x101825u
#define PANEL 0x1b2b3du
#define PANEL_LIGHT 0x223b50u
#define HIGHLIGHT 0x56d7e7u
#define TEXT 0xf1f6fau
#define DIM 0xa5b9cau
#define GOLD 0xffce69u
#define RAIN 0x7bcdf5u

static void rect(uint32_t *dst, unsigned stride, int x, int y, int w, int h, uint32_t col) {
    if (x < 0) w += x, x = 0;
    if (y < 0) h += y, y = 0;
    if (x + w > DASH_WIDTH) w = DASH_WIDTH - x;
    if (y + h > DASH_HEIGHT) h = DASH_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int px = x; px < x + w; px++) {
        for (int py = y; py < y + h; py++)
            dst[px * stride + DASH_HEIGHT - py - 1] = 0xff000000u | col;
    }
}

static void frame(uint32_t *dst, unsigned stride, int x, int y, int w, int h, uint32_t col) {
    rect(dst, stride, x, y, w, 1, col);
    rect(dst, stride, x, y + h - 1, w, 1, col);
    rect(dst, stride, x, y, 1, h, col);
    rect(dst, stride, x + w - 1, y, 1, h, col);
}

static void label(uint32_t *dst, unsigned stride, int x, int y, const char *s, int scale,
                  uint32_t color, int max_chars) {
    int printed = 0;
    for (; *s && printed < max_chars; s++, printed++) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        const unsigned char *font = weather_glyph[c - 32];
        for (int row = 0; row < 12; row++) {
            unsigned bits = font[row];
            for (int col = 0; col < 8; col++)
                if (bits & (128u >> col))
                    rect(dst, stride, x + col * scale, y + row * scale, scale, scale, color);
        }
        x += 8 * scale;
    }
}

static const char *condition(int code) {
    if (code == 0) return "Clear";
    if (code <= 2 && code >= 1) return "Partly cloudy";
    if (code == 3) return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code >= 85 && code <= 86) return "Snow showers";
    if (code >= 95) return "Thunderstorm";
    return "Cloudy";
}

static int icon_index(int code) {
    const char *icon = weather_art_icon(code);
    static const char *const names[] = {
        "sunny", "partly", "cloudy", "overcast", "fog", "drizzle", "rain",
        "showers", "thunder", "snow", "snowshowers", "night", "nightcloud",
        "wind", "humidity"
    };
    for (int i = 0; i < 15; ++i) {
        if (!strcmp(icon, names[i])) return i;
    }
    return 3;
}

static void icon(uint32_t *dst, unsigned stride, int x, int y, int code, int scale) {
    int index = icon_index(code);
    for (int py = 0; py < 24; py++)
        for (int px = 0; px < 24; px++)
            rect(dst, stride, x + px * scale, y + py * scale, scale, scale,
                 weather_sprite[index][py * 24 + px]);
}

static int units(int c, int f) {
    return f ? c * 9 / 5 + 32 : c;
}

static void location(uint32_t *dst, unsigned stride, const Dashboard *ui, int slot, int y,
                     int height) {
    const WeatherLocation *loc = &ui->weather.locations[slot];
    uint32_t fill = slot == ui->slot ? PANEL_LIGHT : PANEL;
    rect(dst, stride, 14, y, 219, height, fill);
    frame(dst, stride, 14, y, 219, height, slot == ui->slot ? HIGHLIGHT : 0x385368u);
    label(dst, stride, 24, y + 5, slot ? "SAVED LOCATION" : "CURRENT LOCATION", 1, DIM, 18);
    if (!loc->used) {
        label(dst, stride, 24, y + 31, slot ? "No saved city" : "Finding location",
              1, TEXT, 24);
        return;
    }
    label(dst, stride, 24, y + 20, loc->name, 1, TEXT, 22);
    if (loc->valid) {
        char temp[24], caption[36];
        snprintf(temp, sizeof temp, "%d%c", units(loc->temperature_c, ui->units_f),
                 ui->units_f ? 'F' : 'C');
        label(dst, stride, 24, y + 36, temp, 2, TEXT, 9);
        snprintf(caption, sizeof caption, "Humidity %d%%", loc->humidity);
        label(dst, stride, 116, y + height - 23, caption, 1, DIM, 14);
    } else {
        label(dst, stride, 24, y + 39, "No weather yet", 1, DIM, 21);
    }
}

static void forecast(uint32_t *dst, unsigned stride, const Dashboard *ui) {
    const WeatherLocation *loc = &ui->weather.locations[ui->slot];
    label(dst, stride, 14, 191, "7-DAY FORECAST", 1, TEXT, 20);
    for (int i = 0; i < Q2_FORECAST_DAYS; i++) {
        int x = 14 + i * 50;
        uint32_t fill = i == ui->day ? PANEL_LIGHT : PANEL;
        rect(dst, stride, x, 209, 46, 74, fill);
        frame(dst, stride, x, 209, 46, 74, i == ui->day ? HIGHLIGHT : 0x345166u);
        if (!loc->forecast_valid) {
            label(dst, stride, x + 11, 237, "--", 1, DIM, 2);
            continue;
        }
        const WeatherDay *d = &loc->days[i];
        label(dst, stride, x + 4, 215, d->date + 5, 1, DIM, 5);
        icon(dst, stride, x + 11, 229, d->weather_code, 1);
        char temp[24];
        snprintf(temp, sizeof temp, "%d", units(d->high_c, ui->units_f));
        label(dst, stride, x + 6, 257, temp, 1, i == ui->day ? GOLD : TEXT, 3);
        snprintf(temp, sizeof temp, "%d", units(d->low_c, ui->units_f));
        label(dst, stride, x + 24, 268, temp, 1, DIM, 3);
    }
}

void dashboard_render(const Dashboard *ui, uint32_t *dst, unsigned stride) {
    rect(dst, stride, 0, 0, DASH_WIDTH, DASH_HEIGHT, BG);
    rect(dst, stride, 0, 0, DASH_WIDTH, 39, PANEL);
    rect(dst, stride, 0, 38, DASH_WIDTH, 1, 0x35546au);
    label(dst, stride, 14, 11, ui->detailed ? "< BACK" : "Q2 WEATHER", 1, TEXT, 22);
    label(dst, stride, 277, 11, "OFFLINE OK", 1, DIM, 12);
    if (!ui->detailed) {
        location(dst, stride, ui, 0, 50, 70);
        location(dst, stride, ui, 1, 126, 57);
        const WeatherLocation *loc = &ui->weather.locations[ui->slot];
        rect(dst, stride, 244, 50, 116, 133, PANEL);
        frame(dst, stride, 244, 50, 116, 133, 0x385368u);
        if (loc->valid) {
            icon(dst, stride, 266, 60, loc->weather_code, 3);
            char temp[22];
            snprintf(temp, sizeof temp, "%d%c", units(loc->temperature_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            label(dst, stride, 269, 138, temp, 2, TEXT, 5);
            label(dst, stride, 256, 169, condition(loc->weather_code), 1, DIM, 13);
        } else {
            label(dst, stride, 263, 113, "NO DATA", 1, DIM, 9);
        }
        forecast(dst, stride, ui);
        label(dst, stride, 14, 293, "WHEEL DAY  CENTER DETAILS  BACK EXIT", 1, DIM, 45);
    } else {
        const WeatherLocation *loc = &ui->weather.locations[ui->slot];
        label(dst, stride, 14, 50, loc->used ? loc->name : "No location", 2, TEXT, 20);
        if (loc->forecast_valid) {
            const WeatherDay *day = &loc->days[ui->day];
            icon(dst, stride, 14, 100, day->weather_code, 4);
            label(dst, stride, 132, 104, day->date, 1, DIM, 11);
            label(dst, stride, 132, 125, condition(day->weather_code), 1,
                  TEXT, 27);
            char text[48];
            snprintf(text, sizeof text, "HIGH %d%c", units(day->high_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            label(dst, stride, 132, 152, text, 2, GOLD, 12);
            snprintf(text, sizeof text, "LOW %d%c", units(day->low_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            label(dst, stride, 132, 182, text, 2, RAIN, 12);
            if (day->precipitation_pct >= 0) {
                snprintf(text, sizeof text, "Rain chance %d%%", day->precipitation_pct);
                label(dst, stride, 132, 214, text, 1, DIM, 27);
            }
        } else {
            label(dst, stride, 22, 113, "Forecast unavailable", 1, DIM, 32);
        }
        label(dst, stride, 14, 281, "WHEEL DAY  CENTER OVERVIEW  BACK", 1, DIM, 42);
    }
}
