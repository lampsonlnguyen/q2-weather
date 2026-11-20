/* SPDX-License-Identifier: MIT */
#include "dashboard.h"
#include "art.h"
#include "lang.h"
#define TR(name) weather_text(WX_##name)

#include <stdio.h>
#include <string.h>

/* Replaceable PNG sprite atlas and antialiased sans-serif glyphs, packed at build time. */
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
    /* iPod-inspired neutral system surfaces, not a fixed cyan/Astra look.
     * Minimal, Classic and Modern each have distinct light/dark surfaces. */
    if (ui->theme_id == 1) { /* Classic: ivory/silver with glossy selection */
        palette = ui->theme_light
                      ? (Palette){0xf8f8f6u, 0xffffffffu, 0xe4e7ebu, 0xa6a8abu,
                                  0x007dbeu, 0x191a1bu, 0x52565bu, 0x976900u, 0x3168a3u}
                      : (Palette){0x111112u, 0x242426u, 0x3b3b3du, 0x5d6066u,
                                  0x5da7eeu, 0xf7f8f9u, 0xd5d7dau, 0xffca63u, 0x91b8edu};
    } else if (ui->theme_id == 2) { /* Modern: flatter iPod-style cards */
        palette = ui->theme_light
                      ? (Palette){0xf5f5f6u, 0xffffffffu, 0xeceef2u, 0xaaaeb6u,
                                  0x234a9du, 0x1a1a1cu, 0x484d58u, 0x875900u, 0x245d9au}
                      : (Palette){0x121316u, 0x22242au, 0x343741u, 0x646a75u,
                                  0xaab5d5u, 0xf8f8fau, 0xc5cad4u, 0xffd06bu, 0x94bff1u};
    } else { /* Minimal: monochrome graphite with the user's accent */
        palette = ui->theme_light
                      ? (Palette){0xf8f8f8u, 0xffffffffu, 0xe8e8e8u, 0xb1b1b1u,
                                  0x4d4d4du, 0x171717u, 0x505050u, 0x845a06u, 0x255e9du}
                      : (Palette){0x101010u, 0x202020u, 0x383838u, 0x707070u,
                                  0xbcbcbcu, 0xf6f6f6u, 0xd1d1d1u, 0xffd477u, 0x9ac8f5u};
    }
    /* Higher contrast than the stock tint for legibility on the small Q2. */
    static const uint32_t dark_accent[] = {
        0xbfc1c4u, 0xff8295u, 0x90dbd4u, 0xe0c88cu, 0x9bbcffu,
        0xffb487u, 0xdb9fcfu, 0xf2e3c1u, 0xb9dc89u, 0x7ebfffu
    };
    static const uint32_t light_accent[] = {
        0x464646u, 0x9c213fu, 0x086a69u, 0x6f5918u, 0x1d4e9au,
        0x8b410au, 0x77294fu, 0x5c4b25u, 0x436521u, 0x075e9cu
    };
    int accent = ui->theme_accent;
    if (accent < 0 || accent > 9) accent = 0;
    palette.highlight = ui->theme_light ? light_accent[accent] : dark_accent[accent];
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

static int text_width(const char *s, int large) {
    int width = 0;
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        width += large ? weather_font_large_advance[c - 32]
                       : weather_font_small_advance[c - 32];
    }
    return width;
}

static const char *condition(int code) {
    if (code == 0) return TR(CLEAR);
    if (code <= 2 && code >= 1) return TR(PARTLY_CLOUDY);
    if (code == 3) return TR(CLOUDY);
    if (code == 45 || code == 48) return TR(FOG);
    if (code >= 51 && code <= 57) return TR(DRIZZLE);
    if (code >= 61 && code <= 67) return TR(RAIN);
    if (code >= 71 && code <= 77) return TR(SNOW);
    if (code >= 80 && code <= 82) return TR(SHOWERS);
    if (code >= 85 && code <= 86) return TR(SNOW_SHOWERS);
    if (code >= 95) return TR(THUNDERSTORM);
    return TR(CLOUDY);
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
    rect(dst, stride, 31, y, 196, height, fill);
    frame(dst, stride, 31, y, 196, height, slot == ui->slot ? HIGHLIGHT : BORDER);
    label(dst, stride, 43, y + 5, slot ? TR(SAVED_LOCATION) : TR(CURRENT_LOCATION), 1, DIM, 18);
    if (!loc->used) {
        label(dst, stride, 43, y + 31, slot ? TR(ADD_CITY) : TR(FINDING_LOCATION),
              1, TEXT, 24);
        return;
    }
    label(dst, stride, 43, y + 20, loc->name, 1, TEXT, 22);
    if (loc->valid) {
        char temp[24], caption[36];
        snprintf(temp, sizeof temp, "%d%c", units(loc->temperature_c, ui->units_f),
                 ui->units_f ? 'F' : 'C');
        label(dst, stride, 43, y + 36, temp, 2, TEXT, 9);
        snprintf(caption, sizeof caption, "%s %d%%", TR(HUMIDITY), loc->humidity);
        label(dst, stride, 43, y + height - 23, caption, 1, DIM, 18);
    } else {
        label(dst, stride, 43, y + 39, TR(LOADING), 1, DIM, 21);
    }
}

/* The entry page mirrors the original Weather app: pick a location first. */
static void location_choice(uint32_t *dst, unsigned stride, const Dashboard *ui,
                            int slot, int y) {
    const WeatherLocation *loc = &ui->weather.locations[slot];
    uint32_t fill = slot == ui->home_selected ? PANEL_LIGHT : PANEL;
    rect(dst, stride, 31, y, 313, 90, fill);
    frame(dst, stride, 31, y, 313, 90, slot == ui->home_selected ? HIGHLIGHT : BORDER);
    label(dst, stride, 43, y + 6, slot ? TR(SAVED_LOCATION) : TR(CURRENT_LOCATION), 1,
          DIM, 21);
    if (loc->used) {
        label(dst, stride, 43, y + 27, loc->name, 2, TEXT, 16);
        if (loc->valid) {
            char details[48];
            snprintf(details, sizeof details, "%d%c  %s", units(loc->temperature_c, ui->units_f),
                     ui->units_f ? 'F' : 'C', condition(loc->weather_code));
            label(dst, stride, 43, y + 64, details, 1, DIM, 28);
            icon(dst, stride, 279, y + 20, loc->weather_code, 2);
        } else {
            label(dst, stride, 43, y + 64, TR(LOADING), 1, DIM, 26);
        }
    } else {
        label(dst, stride, 43, y + 31,
              slot ? TR(ADD_CITY) : TR(FINDING_LOCATION), 2, TEXT, 20);
        label(dst, stride, 43, y + 64,
              slot ? TR(CHOOSE_CITY) : TR(WAITING_LOCATION), 1, DIM, 29);
    }
}

static void forecast(uint32_t *dst, unsigned stride, const Dashboard *ui) {
    const WeatherLocation *loc = &ui->weather.locations[ui->slot];
    label(dst, stride, 33, 191, TR(FORECAST), 1, TEXT, 20);
    for (int i = 0; i < Q2_FORECAST_DAYS; i++) {
        int x = 31 + i * 45;
        uint32_t fill = i == ui->day ? PANEL_LIGHT : PANEL;
        rect(dst, stride, x, 209, 42, 74, fill);
        frame(dst, stride, x, 209, 42, 74, i == ui->day ? HIGHLIGHT : BORDER);
        if (!loc->forecast_valid) {
            label(dst, stride, x + 9, 237, "--", 1, DIM, 2);
            continue;
        }
        const WeatherDay *d = &loc->days[i];
        label(dst, stride, x + 4, 215, d->date + 5, 1, DIM, 5);
        icon(dst, stride, x + 9, 229, d->weather_code, 1);
        char temp[24];
        snprintf(temp, sizeof temp, "%d", units(d->high_c, ui->units_f));
        label(dst, stride, x + 6, 257, temp, 1, i == ui->day ? GOLD : TEXT, 3);
        snprintf(temp, sizeof temp, "%d", units(d->low_c, ui->units_f));
        label(dst, stride, x + 21, 268, temp, 1, DIM, 3);
    }
}

void dashboard_render(const Dashboard *ui, uint32_t *dst, unsigned stride) {
    choose_palette(ui);
    rect(dst, stride, 0, 0, DASH_WIDTH, DASH_HEIGHT, BG);
    rect(dst, stride, 0, 0, DASH_WIDTH, 39, PANEL);
    rect(dst, stride, 0, 38, DASH_WIDTH, 1, BORDER);
    if (ui->page) {
        char back[48];
        snprintf(back, sizeof back, "< %s", TR(BACK));
        label(dst, stride, 42, 12, back, 1, TEXT, 12);
    }
    const char *title = TR(WEATHER);
    label(dst, stride, (DASH_WIDTH - text_width(title, 0)) / 2, 12,
          title, 1, TEXT, 24);
    if (ui->page == 0) {
        int selected = ui->home_selected == 2;
        rect(dst, stride, 266, 9, 68, 25, selected ? PANEL_LIGHT : PANEL);
        frame(dst, stride, 266, 9, 68, 25, selected ? HIGHLIGHT : BORDER);
        label(dst, stride, 300 - text_width(TR(SETTINGS), 0) / 2, 12,
              TR(SETTINGS), 1, selected ? HIGHLIGHT : DIM, 14);
    } else
        label(dst, stride, 279, 12, "Q2 POD", 1, DIM, 8);
    if (ui->page == 3) {
        label(dst, stride, 43, 49, ui->city_target ? TR(SEARCH_CITY) : "SET HOME CITY",
              1, TEXT, 23);
        rect(dst, stride, 33, 72, 309, 34, PANEL);
        frame(dst, stride, 33, 72, 309, 34, HIGHLIGHT);
        label(dst, stride, 45, 80, ui->city_length ? ui->city_query : "City name...",
              1, ui->city_length ? TEXT : DIM, 35);
        static const char *const letters[] = {
            "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"
        };
        static const char *const numbers[] = {
            "1234567890", "-./'@#&()", "+*?=!:,"
        };
        const char *const *rows = ui->keyboard_numbers ? numbers : letters;
        const int offsets[] = {32, 47, 78};
        const int starts[] = {0, 10, 19};
        const int tops[] = {115, 154, 193};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; rows[row][col]; ++col) {
                int index = starts[row] + col;
                int x = offsets[row] + col * 31;
                int y = tops[row];
                int selected = ui->keyboard_index == index;
                rect(dst, stride, x, y, 29, 32, selected ? HIGHLIGHT : PANEL_LIGHT);
                frame(dst, stride, x, y, 29, 32, selected ? HIGHLIGHT : BORDER);
                char letter[] = {rows[row][col], 0};
                int width = text_width(letter, 0);
                label(dst, stride, x + (29 - width) / 2, y + 8, letter, 1,
                      selected ? BG : TEXT, 1);
            }
        }
        /* The action row has five buttons safely inside the rounded screen.
         * Backspace is drawn as a pixel arrow, not a text substitute. */
        for (int i = 0; i < 5; ++i) {
            int x = 34 + i * 62;
            int selected = ui->keyboard_index == 26 + i;
            uint32_t ink = selected ? BG : TEXT;
            rect(dst, stride, x, 238, 59, 37, selected ? HIGHLIGHT : PANEL);
            frame(dst, stride, x, 238, 59, 37, selected ? HIGHLIGHT : BORDER);
            if (i == 3) {
                /* Smaller, centered backspace arrow; button hitbox is unchanged. */
                rect(dst, stride, x + 23, 255, 15, 2, ink);
                for (int d = 0; d < 5; ++d) {
                    rect(dst, stride, x + 23 + d, 255 - d, 2, 2, ink);
                    rect(dst, stride, x + 23 + d, 255 + d, 2, 2, ink);
                }
            } else {
                const char *text = i == 0 ? (ui->keyboard_numbers ? "ABC" : "123")
                                   : i == 1 ? "SPACE"
                                   : i == 2 ? "CLEAR" : "SEARCH";
                label(dst, stride, x + (59 - text_width(text, 0)) / 2,
                      248, text, 1, ink, 8);
            }
        }
        label(dst, stride, 42, 289, "WHEEL SELECT   CENTER TYPE   BACK", 1, DIM, 39);
        return;
    }
    if (ui->page == 5) {
        label(dst, stride, 43, 49, "SELECT LOCATION", 1, TEXT, 21);
        char summary[55];
        snprintf(summary, sizeof summary, "%d matches - select exact city",
                 ui->match_count);
        label(dst, stride, 43, 72, summary, 1, DIM, 36);
        int start = (ui->match_selected / 3) * 3;
        for (int row = 0; row < 3 && start + row < ui->match_count; ++row) {
            int idx = start + row;
            int y = 94 + row * 60;
            int selected = idx == ui->match_selected;
            rect(dst, stride, 34, y, 307, 54, selected ? PANEL_LIGHT : PANEL);
            frame(dst, stride, 34, y, 307, 54, selected ? HIGHLIGHT : BORDER);
            label(dst, stride, 46, y + 3, ui->matches[idx].name,
                  1, selected ? HIGHLIGHT : TEXT, 32);
            label(dst, stride, 46, y + 26, ui->matches[idx].region,
                  1, DIM, 36);
            label(dst, stride, 322, y + 12, ">", 1, HIGHLIGHT, 1);
        }
        if (ui->match_count > 3) {
            char progress[20];
            snprintf(progress, sizeof progress, "%d / %d", ui->match_selected + 1,
                     ui->match_count);
            label(dst, stride, 275, 277, progress, 1, DIM, 12);
        }
        label(dst, stride, 43, 290, "WHEEL  CENTER SELECT  BACK", 1, DIM, 35);
        return;
    }
    if (ui->page == 2) {
        label(dst, stride, 40, 47, TR(SETTINGS), 2, TEXT, 16);
        const char *titles[] = {TR(CHANGE_CITY), TR(UNITS), TR(REFRESH),
                                TR(CLEAR_CITY), "Set Home City"};
        for (int i = 0; i < 5; i++) {
            int y = 76 + i * 48;
            rect(dst, stride, 31, y, 313, 43, ui->keyboard_index == i ? PANEL_LIGHT : PANEL);
            frame(dst, stride, 31, y, 313, 43,
                  ui->keyboard_index == i ? HIGHLIGHT : BORDER);
            label(dst, stride, 43, y + 12, titles[i], 1, TEXT, 25);
            if (i == 1)
                label(dst, stride, 292, y + 12, ui->units_f ? "FAHR" : "CELS", 1,
                      HIGHLIGHT, 5);
        }
        /* The fifth setting occupies the previous footer area. */
        return;
    }
    if (ui->page == 0) {
        label(dst, stride, 37, 49, TR(LOCATIONS), 1, DIM, 15);
        location_choice(dst, stride, ui, 0, 65);
        location_choice(dst, stride, ui, 1, 165);
        label(dst, stride, 40, 276,
              ui->network_status[0] ? ui->network_status : TR(SELECT_LOCATION),
              1, ui->network_status[0] ? GOLD : DIM, 47);
        label(dst, stride, 42, 289, "CENTER OPEN   BACK EXIT", 1, DIM, 33);
        return;
    }
    if (ui->page == 1) {
        location(dst, stride, ui, ui->slot, 50, 133);
        const WeatherLocation *loc = &ui->weather.locations[ui->slot];
        rect(dst, stride, 237, 50, 107, 133, PANEL);
        frame(dst, stride, 237, 50, 107, 133, BORDER);
        if (loc->valid) {
            icon(dst, stride, 255, 60, loc->weather_code, 3);
            char temp[22];
            snprintf(temp, sizeof temp, "%d%c", units(loc->temperature_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            label(dst, stride, 250, 138, temp, 2, TEXT, 5);
            label(dst, stride, 243, 165, condition(loc->weather_code), 1, DIM, 13);
        } else {
            label(dst, stride, 258, 113, TR(NO_DATA), 1, DIM, 9);
        }
        forecast(dst, stride, ui);
        label(dst, stride, 42, 289,
              ui->network_status[0] ? ui->network_status : "CENTER DETAILS   BACK LOCATIONS",
              1, ui->network_status[0] ? GOLD : DIM, 39);
    } else {
        const WeatherLocation *loc = &ui->weather.locations[ui->slot];
        label(dst, stride, 40, 47, loc->used ? loc->name : "No location", 2, TEXT, 20);
        if (loc->forecast_valid) {
            const WeatherDay *day = &loc->days[ui->day];
            icon(dst, stride, 39, 104, day->weather_code, 4);
            label(dst, stride, 152, 104, day->date, 1, DIM, 11);
            label(dst, stride, 152, 125, condition(day->weather_code), 1,
                  TEXT, 27);
            char text[48];
            snprintf(text, sizeof text, "%s %d%c", TR(HIGH), units(day->high_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            label(dst, stride, 152, 152, text, 2, GOLD, 12);
            snprintf(text, sizeof text, "%s %d%c", TR(LOW), units(day->low_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            label(dst, stride, 152, 182, text, 2, RAIN, 12);
            if (day->precipitation_pct >= 0) {
                snprintf(text, sizeof text, "%s %d%%", TR(RAIN_CHANCE), day->precipitation_pct);
                label(dst, stride, 152, 214, text, 1, DIM, 27);
            }
        } else {
            label(dst, stride, 42, 113, TR(FORECAST_UNAVAILABLE), 1, DIM, 32);
        }
        label(dst, stride, 42, 289, "CENTER FORECAST   BACK", 1, DIM, 32);
    }
}
