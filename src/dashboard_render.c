/* SPDX-License-Identifier: MIT */
#include "dashboard.h"
#include "art.h"

#include <stdio.h>
#include <string.h>

/* Generated 24x24 pixels and an 8x12 bitmap font. No runtime decoding. */
#include "weather_sprites.inc"

/* Separate high-contrast palettes; Q2 settings determine the active half.
 * Palette accents track the firmware's selected theme/accent family. */
typedef struct { uint32_t bg, panel, raised, outline, highlight, text, muted, gold, rain; } Palette;
static Palette palette;
#define BG palette.bg
#define PANEL palette.panel
#define PANEL_LIGHT palette.raised
#define BORDER palette.outline
#define HIGHLIGHT palette.highlight
#define TEXT palette.text
#define DIM palette.muted
#define GOLD palette.gold
#define RAIN palette.rain

static void choose_palette(const Dashboard *ui) {
    if (ui->theme_light) {
        palette = (Palette){0xf7f8fau, 0xffffffffu, 0xe9f2f7u,
                            0x9eb7c7u, 0x075e82u, 0x172738u, 0x425667u,
                            0x825000u, 0x075e9au};
    } else {
        palette = (Palette){0x111b28u, 0x202c3du, 0x304459u,
                            0x668198u, 0x80dcf1u, 0xf5f8fcu, 0xc4d1deu,
                            0xffd16bu, 0x83c6f5u};
    }
    static const uint32_t dark_accent[] = {0xaab6c5u, 0xff8893u, 0x80dcf1u,
                                           0xffd48fu, 0x92b8ffu, 0xffb47eu,
                                           0xcf9dffu, 0xf5f0d5u, 0x88dbaeu};
    static const uint32_t light_accent[] = {0x455668u, 0x972d40u, 0x075e82u,
                                            0x825000u, 0x244a90u, 0x93420au,
                                            0x63368fu, 0x5a4c2bu, 0x176944u};
    int i = ui->theme_accent;
    if (i < 0 || i >= 9) i = ui->theme_id == 2 ? 4 : 2;
    palette.highlight = ui->theme_light ? light_accent[i] : dark_accent[i];
}

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

static uint32_t blend(uint32_t back, uint32_t front, unsigned alpha) {
    unsigned inv = 255 - alpha;
    unsigned r = (((back >> 16) & 255) * inv + ((front >> 16) & 255) * alpha + 127) / 255;
    unsigned g = (((back >> 8) & 255) * inv + ((front >> 8) & 255) * alpha + 127) / 255;
    unsigned b = ((back & 255) * inv + (front & 255) * alpha + 127) / 255;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static void pixel_blend(uint32_t *dst, unsigned stride, int x, int y,
                        uint32_t color, unsigned alpha) {
    if (x < 0 || y < 0 || x >= DASH_WIDTH || y >= DASH_HEIGHT || !alpha) return;
    uint32_t *p = &dst[(unsigned)x * stride + DASH_HEIGHT - y - 1];
    *p = blend(*p, color, alpha);
}

/* Proportional, antialiased DejaVu Sans; rasterized at build time.
 * Only alpha coverage lives on the Q2, not a font engine. */
static void label(uint32_t *dst, unsigned stride, int x, int y, const char *s, int scale,
                  uint32_t color, int max_chars) {
    int large = scale > 1;
    for (int printed = 0; *s && printed < max_chars; s++, printed++) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        unsigned index = (unsigned)c - 32;
        if (large) {
            for (int py = 0; py < 30; py++)
                for (int px = 0; px < 23; px++)
                    pixel_blend(dst, stride, x + px, y + py, color,
                                weather_font_large[index][py][px]);
            x += weather_font_large_advance[index];
        } else {
            for (int py = 0; py < 18; py++)
                for (int px = 0; px < 13; px++)
                    pixel_blend(dst, stride, x + px, y + py, color,
                                weather_font_small[index][py][px]);
            x += weather_font_small_advance[index];
        }
        if (x >= DASH_WIDTH - 4) break;
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
        for (int px = 0; px < 24; px++) {
            uint32_t argb = weather_sprite[index][py * 24 + px];
            unsigned alpha = argb >> 24;
            if (!alpha) continue;
            for (int yy = 0; yy < scale; yy++)
                for (int xx = 0; xx < scale; xx++)
                    pixel_blend(dst, stride, x + px * scale + xx,
                                y + py * scale + yy, argb & 0xffffffu, alpha);
        }
}

static int units(int c, int f) {
    return f ? c * 9 / 5 + 32 : c;
}

static void location(uint32_t *dst, unsigned stride, const Dashboard *ui, int slot, int y,
                     int height) {
    const WeatherLocation *loc = &ui->weather.locations[slot];
    uint32_t fill = slot == ui->slot ? PANEL_LIGHT : PANEL;
    rect(dst, stride, 14, y, 219, height, fill);
    frame(dst, stride, 14, y, 219, height, slot == ui->slot ? HIGHLIGHT : BORDER);
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

/* The entry page mirrors the original Weather app: pick a location first. */
static void location_choice(uint32_t *dst, unsigned stride, const Dashboard *ui,
                            int slot, int y) {
    const WeatherLocation *loc = &ui->weather.locations[slot];
    uint32_t fill = slot == ui->slot ? PANEL_LIGHT : PANEL;
    rect(dst, stride, 14, y, 346, 90, fill);
    frame(dst, stride, 14, y, 346, 90, slot == ui->slot ? HIGHLIGHT : BORDER);
    label(dst, stride, 27, y + 6, slot ? "SAVED LOCATION" : "CURRENT LOCATION", 1,
          DIM, 21);
    if (loc->used) {
        label(dst, stride, 27, y + 27, loc->name, 2, TEXT, 25);
        if (loc->valid) {
            char details[48];
            snprintf(details, sizeof details, "%d%c  %s", units(loc->temperature_c, ui->units_f),
                     ui->units_f ? 'F' : 'C', condition(loc->weather_code));
            label(dst, stride, 27, y + 64, details, 1, DIM, 28);
            icon(dst, stride, 288, y + 20, loc->weather_code, 2);
        } else {
            label(dst, stride, 27, y + 64, "Loading weather...", 1, DIM, 26);
        }
    } else {
        label(dst, stride, 27, y + 31,
              slot ? "Add saved city" : "Locating...", 2, TEXT, 20);
        label(dst, stride, 27, y + 64,
              slot ? "Choose a city to track" : "Waiting for location", 1, DIM, 29);
    }
}

static void forecast(uint32_t *dst, unsigned stride, const Dashboard *ui) {
    const WeatherLocation *loc = &ui->weather.locations[ui->slot];
    label(dst, stride, 14, 191, "7-DAY FORECAST", 1, TEXT, 20);
    for (int i = 0; i < Q2_FORECAST_DAYS; i++) {
        int x = 14 + i * 50;
        uint32_t fill = i == ui->day ? PANEL_LIGHT : PANEL;
        rect(dst, stride, x, 209, 46, 74, fill);
        frame(dst, stride, x, 209, 46, 74, i == ui->day ? HIGHLIGHT : BORDER);
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
    choose_palette(ui);
    rect(dst, stride, 0, 0, DASH_WIDTH, DASH_HEIGHT, BG);
    rect(dst, stride, 0, 0, DASH_WIDTH, 39, PANEL);
    rect(dst, stride, 0, 38, DASH_WIDTH, 1, BORDER);
    label(dst, stride, 14, 11, ui->page ? "< BACK" : "Q2 WEATHER", 1, TEXT, 22);
    if (ui->page == 0)
        label(dst, stride, 287, 11, "SETTINGS", 1, HIGHLIGHT, 8);
    else
        label(dst, stride, 287, 11, "WEATHER", 1, DIM, 8);
    if (ui->page == 3) {
        label(dst, stride, 14, 47, "SEARCH CITY", 2, TEXT, 20);
        label(dst, stride, 14, 67, ui->city_query, 1, HIGHLIGHT, 40);
        const char *keys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _<+";
        for (int i = 0; keys[i]; ++i) {
            int x = 14 + (i % 6) * 58, y = 80 + (i / 6) * 32;
            rect(dst, stride, x, y, 53, 29, ui->keyboard_index == i ? PANEL_LIGHT : PANEL);
            frame(dst, stride, x, y, 53, 29, ui->keyboard_index == i ? HIGHLIGHT : BORDER);
            char letter[2] = {keys[i], 0};
            if (letter[0] == '_') letter[0] = ' ';
            label(dst, stride, x + 20, y + 7, letter, 1, TEXT, 1);
        }
        label(dst, stride, 14, 300, "< BACKSPACE  + SEARCH", 1, DIM, 32);
        return;
    }
    if (ui->page == 2) {
        label(dst, stride, 14, 47, "SETTINGS", 2, TEXT, 16);
        const char *titles[] = {"CHANGE SAVED CITY", "UNITS", "REFRESH WEATHER", "CLEAR SAVED CITY"};
        for (int i = 0; i < 4; i++) {
            int y = 76 + i * 48;
            rect(dst, stride, 14, y, 346, 43, ui->keyboard_index == i ? PANEL_LIGHT : PANEL);
            frame(dst, stride, 14, y, 346, 43,
                  ui->keyboard_index == i ? HIGHLIGHT : BORDER);
            label(dst, stride, 24, y + 12, titles[i], 1, TEXT, 25);
            if (i == 1)
                label(dst, stride, 292, y + 12, ui->units_f ? "FAHR" : "CELS", 1,
                      HIGHLIGHT, 5);
        }
        if (ui->search_result == 1)
            label(dst, stride, 14, 277, "Searching city...", 1, GOLD, 34);
        else if (ui->search_result == 2)
            label(dst, stride, 14, 277, "Saved city updated", 1, HIGHLIGHT, 34);
        else if (ui->search_result == -1)
            label(dst, stride, 14, 277, "City not found - retry", 1, GOLD, 34);
        label(dst, stride, 14, 300, "ICONS: TOMORROW.IO  BACK TO LOCATIONS", 1, DIM, 40);
        return;
    }
    if (ui->page == 0) {
        label(dst, stride, 14, 48, "LOCATIONS", 1, DIM, 15);
        location_choice(dst, stride, ui, 0, 65);
        location_choice(dst, stride, ui, 1, 165);
        label(dst, stride, 14, 276, "SELECT A LOCATION TO VIEW FORECAST", 1, DIM, 47);
        label(dst, stride, 14, 298, "WHEEL SELECT  CENTER OPEN  BACK EXIT", 1, DIM, 45);
        return;
    }
    if (ui->page == 1) {
        location(dst, stride, ui, ui->slot, 50, 133);
        const WeatherLocation *loc = &ui->weather.locations[ui->slot];
        rect(dst, stride, 244, 50, 116, 133, PANEL);
        frame(dst, stride, 244, 50, 116, 133, BORDER);
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
        label(dst, stride, 14, 293, "WHEEL DAY  CENTER DETAILS  BACK LOCATIONS", 1, DIM, 45);
    } else {
        const WeatherLocation *loc = &ui->weather.locations[ui->slot];
        label(dst, stride, 14, 45, loc->used ? loc->name : "No location", 2, TEXT, 20);
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
        label(dst, stride, 14, 281, "WHEEL DAY  CENTER FORECAST  BACK", 1, DIM, 42);
    }
}
