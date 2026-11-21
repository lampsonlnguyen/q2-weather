/* SPDX-License-Identifier: MIT */
#include "dashboard.h"
#include "lang.h"
#include "q2pod_scheme.h"
#define TR(name) weather_text(WX_##name)

#include <stdio.h>
#include <string.h>

/* q2-pod's own look, inherited from the firmware theme — no invented
 * palettes, no pictorial icons. src/q2pod_scheme.c ports the scheme data
 * (ACCENTS tones, selection painters, paper/ink, bar) from DiamondBond/q2-pod
 * patch/offsets.inc, patch/theme.h and assets/themes/theme.xml; the
 * render layer below only applies it. Conditions are text only.
 * Fonts stay the existing build-time glyph rasterizer (a font engine, not
 * new graphics); sizes/weights/caps follow the theme roles. */
#include "weather_fonts.inc"

static Q2PodScheme scheme;
#define PAPER scheme.paper
#define INK scheme.ink
#define MUTED scheme.muted
#define FILL scheme.fill
#define SEL_TEXT (scheme.sel_text ? scheme.sel_text : scheme.ink)
#define TEXT_X scheme.text_x
#define BAR_H Q2POD_BAR_H
#define ROW_H Q2POD_ROW_H
/* Classic's list text ends at text_end 350 (classic-light/theme.xml). */
#define TEXT_END 350

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

static void pixel_blend(uint32_t *dst, unsigned stride, int x, int y, uint32_t color,
                        unsigned alpha) {
    if (x < 0 || y < 0 || x >= DASH_WIDTH || y >= DASH_HEIGHT || !alpha) return;
    uint32_t *p = &dst[(unsigned)x * stride + DASH_HEIGHT - y - 1];
    *p = blend(*p, color, alpha);
}

/* Proportional, antialiased DejaVu Sans; rasterized at build time.
 * Only alpha coverage lives on the Q2, not a font engine. */
static void label_styled(uint32_t *dst, unsigned stride, int x, int y, const char *s, int scale,
                         uint32_t color, int max_chars, int caps) {
    int large = scale > 1;
    for (int printed = 0; *s && printed < max_chars; s++, printed++) {
        unsigned char c = (unsigned char)*s;
        if (caps && c >= 'a' && c <= 'z') c -= 'a' - 'A';
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

static void label(uint32_t *dst, unsigned stride, int x, int y, const char *s, int scale,
                  uint32_t color, int max_chars) {
    label_styled(dst, stride, x, y, s, scale, color, max_chars, 0);
}

static int text_width(const char *s, int large) {
    int width = 0;
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        width += large ? weather_font_large_advance[c - 32] : weather_font_small_advance[c - 32];
    }
    return width;
}

/* Right-align text so its last column is right_x (Classic text_end 350). */
static void label_right(uint32_t *dst, unsigned stride, const char *s, int large, uint32_t color,
                        int right_x, int y) {
    label(dst, stride, right_x - text_width(s, large), y, s, large, color, 64);
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

/* Linear interpolation between two stops, for the gloss bands. */
static uint32_t stop_color(const Q2PodStop *stops, int n, int pos) {
    if (pos <= stops[0].pos) return stops[0].rgb;
    for (int i = 1; i < n; i++) {
        if (pos <= stops[i].pos) {
            int span = stops[i].pos - stops[i - 1].pos;
            int t = span ? (pos - stops[i - 1].pos) * 255 / span : 255;
            uint32_t a = stops[i - 1].rgb, b = stops[i].rgb;
            unsigned r = (((a >> 16) & 255) * (255 - t) + ((b >> 16) & 255) * t) / 255;
            unsigned g = (((a >> 8) & 255) * (255 - t) + ((b >> 8) & 255) * t) / 255;
            unsigned bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
            return (r << 16) | (g << 8) | bl;
        }
    }
    return stops[n - 1].rgb;
}

/* The theme's selection painter (patch/theme.h PAINT_SELECTION) over a row.
 * tall: a row taller than the set's 32 px pitch takes the tall top/edge rows. */
static void paint_selection(uint32_t *dst, unsigned stride, int x, int y, int w, int h) {
    if (scheme.painter == Q2POD_SEL_CARD) {
        /* Minimal's card: SEL_INSET px in from each side, SEL_RADIUS corners
         * (patch/offsets.inc). */
        const int inset = 6, radius = 6;
        int x0 = x + inset, y0 = y + inset, w0 = w - 2 * inset, h0 = h - 2 * inset;
        if (w0 <= 0 || h0 <= 0) return;
        for (int px = x0; px < x0 + w0; px++) {
            for (int py = y0; py < y0 + h0; py++) {
                int cx = px < x0 + radius         ? x0 + radius - px
                         : px >= x0 + w0 - radius ? px - (x0 + w0 - radius - 1)
                                                  : 0;
                int cy = py < y0 + radius         ? y0 + radius - py
                         : py >= y0 + h0 - radius ? py - (y0 + h0 - radius - 1)
                                                  : 0;
                if (cx * cx + cy * cy > radius * radius) continue;
                dst[px * stride + DASH_HEIGHT - py - 1] = 0xff000000u | FILL;
            }
        }
        return;
    }
    if (scheme.painter == Q2POD_SEL_BAR) {
        /* Modern's bar: the accent's flat top tone over the row. */
        rect(dst, stride, x, y, w, h, scheme.accent_top);
        return;
    }
    /* Classic's stops: a crisp top line, the gloss band, and the edge row;
     * a tall row takes its second row and last two rows (scheme_t). */
    int tall = h > ROW_H;
    for (int row = 0; row < h; row++) {
        uint32_t col;
        if (row == 0)
            col = scheme.sel_top;
        else if (row == h - 1)
            col = scheme.sel_edge;
        else if (tall && row == 1)
            col = scheme.sel_tall_top;
        else if (tall && row == h - 2)
            col = scheme.sel_tall_line;
        else
            col = stop_color(scheme.sel, Q2POD_GLOSS_STOPS, row * 100 / (h - 1));
        rect(dst, stride, x, y + row, w, 1, col);
    }
}

/* A list row: paper, or the theme's selection when selected. The chevron
 * (">", text — never a drawn glyph) sits on the selected row only, as in
 * Classic (chevron selected=1); Minimal adds its accent dot at HOME_DOT_X. */
static void list_row(uint32_t *dst, unsigned stride, int y, int h, int selected) {
    if (selected) {
        paint_selection(dst, stride, 0, y, DASH_WIDTH, h);
        if (scheme.painter == Q2POD_SEL_CARD)
            rect(dst, stride, 25, y + h / 2 - 3, 6, 6, scheme.accent_top); /* HOME_DOT */
        label(dst, stride, 317, y + (h - 18) / 2, ">", 0,
              scheme.painter == Q2POD_SEL_CARD ? scheme.chevron : SEL_TEXT, 1);
    }
}

/* A detail row: caption on the left, value right-aligned like q2-pod's
 * settings values (classic-light/theme.xml: names in ink, values grey). */
static void detail_row(uint32_t *dst, unsigned stride, int y, const char *title, const char *value,
                       int selected) {
    list_row(dst, stride, y, ROW_H, selected);
    uint32_t ink = selected ? SEL_TEXT : INK;
    label_styled(dst, stride, TEXT_X, y + 7, title, 0, ink, 25, scheme.caps_rows);
    /* A selected row's value stops short of the chevron (Classic chevron x 317). */
    label_right(dst, stride, value, 0, selected ? SEL_TEXT : MUTED, selected ? 310 : TEXT_END,
                y + 7);
}

static int units(int c, int f) { return f ? c * 9 / 5 + 32 : c; }

/* A location as two-line list rows: meta label, name, status. */
static void location_rows(uint32_t *dst, unsigned stride, const Dashboard *ui, int slot, int y,
                          int selected) {
    const WeatherLocation *loc = &ui->weather.locations[slot];
    list_row(dst, stride, y, 64, selected);
    uint32_t ink = selected ? SEL_TEXT : INK;
    uint32_t dim = selected ? SEL_TEXT : MUTED;
    label_styled(dst, stride, TEXT_X, y + 4, slot ? TR(SAVED_LOCATION) : TR(CURRENT_LOCATION), 0,
                 dim, 21, scheme.caps_rows);
    if (!loc->used) {
        label(dst, stride, TEXT_X, y + 22, slot ? TR(ADD_CITY) : TR(FINDING_LOCATION), 0, ink, 24);
        return;
    }
    label(dst, stride, TEXT_X, y + 22, loc->name, 0, ink, 28);
    if (loc->valid) {
        char status[64];
        snprintf(status, sizeof status, "%d%c  %s  %s %d%%", units(loc->temperature_c, ui->units_f),
                 ui->units_f ? 'F' : 'C', condition(loc->weather_code), TR(HUMIDITY),
                 loc->humidity);
        label(dst, stride, TEXT_X, y + 42, status, 0, dim, 36);
    } else {
        label(dst, stride, TEXT_X, y + 42, TR(LOADING), 0, dim, 21);
    }
}

static void paint_bar(uint32_t *dst, unsigned stride, const Dashboard *ui) {
    if (scheme.painter == Q2POD_SEL_STOPS) {
        /* Classic's gloss bar: the stops, the light half's second line
         * above the line row, then the line (classic-light/theme.xml). */
        for (int row = 0; row < BAR_H - 1; row++) {
            uint32_t col = stop_color(scheme.bar, Q2POD_BAR_STOPS, row * 100 / (BAR_H - 2));
            rect(dst, stride, 0, row, DASH_WIDTH, 1, col);
        }
        if (scheme.bar_line2) rect(dst, stride, 0, BAR_H - 2, DASH_WIDTH, 1, scheme.bar_line2);
        rect(dst, stride, 0, BAR_H - 1, DASH_WIDTH, 1, scheme.bar_line);
    } else {
        /* Minimal / Modern: the flat bar fill (MINIMAL_FILL / LIGHT_BAR). */
        rect(dst, stride, 0, 0, DASH_WIDTH, BAR_H, scheme.bar[0].rgb);
    }
    if (ui->page) {
        char back[48];
        snprintf(back, sizeof back, "< %s", TR(BACK));
        label(dst, stride, 12, 4, back, 0, INK, 12);
    }
    const char *title = TR(WEATHER);
    label_styled(dst, stride, (DASH_WIDTH - text_width(title, 0)) / 2, 4, title, 0, INK, 24,
                 scheme.caps_bar);
    if (ui->page) label_right(dst, stride, "Q2 POD", 0, MUTED, DASH_WIDTH - 12, 4);
}

void dashboard_render(const Dashboard *ui, uint32_t *dst, unsigned stride) {
    q2pod_scheme_resolve(ui->theme_id, ui->theme_accent, ui->theme_light, &scheme);
    rect(dst, stride, 0, 0, DASH_WIDTH, DASH_HEIGHT, PAPER);
    paint_bar(dst, stride, ui);
    if (ui->page == 3) {
        label_styled(dst, stride, TEXT_X, 49, ui->city_target ? TR(SEARCH_CITY) : "SET HOME CITY",
                     0, INK, 23, scheme.caps_rows);
        rect(dst, stride, 33, 72, 309, 34, PAPER);
        frame(dst, stride, 33, 72, 309, 34, scheme.accent_top);
        label(dst, stride, 45, 80, ui->city_length ? ui->city_query : "City name...", 0,
              ui->city_length ? INK : MUTED, 35);
        static const char *const letters[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
        static const char *const numbers[] = { "1234567890", "-./'@#&()", "+*?=!:," };
        const char *const *rows = ui->keyboard_numbers ? numbers : letters;
        const int offsets[] = { 32, 47, 78 };
        const int starts[] = { 0, 10, 19 };
        const int tops[] = { 115, 154, 193 };
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; rows[row][col]; ++col) {
                int index = starts[row] + col;
                int x = offsets[row] + col * 31;
                int y = tops[row];
                int selected = ui->keyboard_index == index;
                if (selected)
                    paint_selection(dst, stride, x, y, 29, 32);
                else {
                    rect(dst, stride, x, y, 29, 32, PAPER);
                    frame(dst, stride, x, y, 29, 32, MUTED);
                }
                char letter[] = { rows[row][col], 0 };
                int width = text_width(letter, 0);
                label(dst, stride, x + (29 - width) / 2, y + 8, letter, 0,
                      selected ? SEL_TEXT : INK, 1);
            }
        }
        /* The action row keeps its five buttons; backspace is the text
         * label DEL now, never a pixel-drawn arrow. */
        for (int i = 0; i < 5; ++i) {
            int x = 34 + i * 62;
            int selected = ui->keyboard_index == 26 + i;
            uint32_t ink = selected ? SEL_TEXT : INK;
            if (selected)
                paint_selection(dst, stride, x, 238, 59, 37);
            else {
                rect(dst, stride, x, 238, 59, 37, PAPER);
                frame(dst, stride, x, 238, 59, 37, MUTED);
            }
            const char *text = i == 0   ? (ui->keyboard_numbers ? "ABC" : "123")
                               : i == 1 ? "SPACE"
                               : i == 2 ? "CLEAR"
                               : i == 3 ? "DEL"
                                        : "SEARCH";
            label(dst, stride, x + (59 - text_width(text, 0)) / 2, 248, text, 0, ink, 8);
        }
        label(dst, stride, 42, 289, "WHEEL SELECT   CENTER TYPE   BACK", 0, MUTED, 39);
        return;
    }
    if (ui->page == 5) {
        label_styled(dst, stride, TEXT_X, 33, "SELECT LOCATION", 0, INK, 21, scheme.caps_rows);
        char summary[55];
        snprintf(summary, sizeof summary, "%d matches - select exact city", ui->match_count);
        label(dst, stride, TEXT_X, 58, summary, 0, MUTED, 36);
        int start = (ui->match_selected / 3) * 3;
        for (int row = 0; row < 3 && start + row < ui->match_count; ++row) {
            int idx = start + row;
            int y = 90 + row * 64;
            int selected = idx == ui->match_selected;
            list_row(dst, stride, y, 64, selected);
            uint32_t ink = selected ? SEL_TEXT : INK;
            uint32_t dim = selected ? SEL_TEXT : MUTED;
            label(dst, stride, TEXT_X, y + 6, ui->matches[idx].name, 0, ink, 32);
            label(dst, stride, TEXT_X, y + 34, ui->matches[idx].region, 0, dim, 36);
        }
        if (ui->match_count > 3) {
            char progress[32];
            snprintf(progress, sizeof progress, "%d / %d", ui->match_selected + 1, ui->match_count);
            label_right(dst, stride, progress, 0, MUTED, TEXT_END, 290);
        }
        label(dst, stride, TEXT_X, 300, "WHEEL  CENTER SELECT  BACK", 0, MUTED, 35);
        return;
    }
    if (ui->page == 2) {
        const char *titles[] = { TR(CHANGE_CITY), TR(UNITS), TR(REFRESH), TR(CLEAR_CITY),
                                 "Set Home City" };
        for (int i = 0; i < 5; i++) {
            int y = BAR_H + i * ROW_H;
            list_row(dst, stride, y, ROW_H, ui->keyboard_index == i);
            uint32_t ink = ui->keyboard_index == i ? SEL_TEXT : INK;
            label_styled(dst, stride, TEXT_X, y + 7, titles[i], 0, ink, 25, scheme.caps_rows);
            if (i == 1)
                label_right(dst, stride, ui->units_f ? "FAHR" : "CELS", 0,
                            ui->keyboard_index == i ? SEL_TEXT : MUTED,
                            ui->keyboard_index == i ? 310 : TEXT_END, y + 7);
        }
        return;
    }
    if (ui->page == 0) {
        location_rows(dst, stride, ui, 0, BAR_H, ui->home_selected == 0);
        location_rows(dst, stride, ui, 1, BAR_H + 64, ui->home_selected == 1);
        int y = BAR_H + 128;
        list_row(dst, stride, y, ROW_H, ui->home_selected == 2);
        label_styled(dst, stride, TEXT_X, y + 7, TR(SETTINGS), 0,
                     ui->home_selected == 2 ? SEL_TEXT : INK, 14, scheme.caps_rows);
        label(dst, stride, TEXT_X, 296, TR(SELECT_LOCATION), 0, MUTED, 47);
        return;
    }
    if (ui->page == 1) {
        const WeatherLocation *loc = &ui->weather.locations[ui->slot];
        label_styled(dst, stride, TEXT_X, BAR_H + 4, loc->used ? loc->name : "No location", 0, INK,
                     28, scheme.caps_rows);
        if (loc->valid) {
            char humidity[24];
            snprintf(humidity, sizeof humidity, "%s %d%%", TR(HUMIDITY), loc->humidity);
            label_right(dst, stride, humidity, 0, MUTED, TEXT_END, BAR_H + 4);
            char temp[22];
            snprintf(temp, sizeof temp, "%d%c", units(loc->temperature_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            label(dst, stride, TEXT_X, BAR_H + 24, temp, 2, INK, 5);
            label(dst, stride, TEXT_X + text_width(temp, 1) + 12, BAR_H + 34,
                  condition(loc->weather_code), 0, MUTED, 13);
        } else {
            label(dst, stride, TEXT_X, BAR_H + 30, TR(NO_DATA), 0, MUTED, 9);
        }
        /* Forecast as seven 32 px list rows: day, condition text, high/low.
         * No pictorial icons; the condition is text only. */
        for (int i = 0; i < Q2_FORECAST_DAYS; i++) {
            int y = BAR_H + 64 + i * ROW_H;
            list_row(dst, stride, y, ROW_H, i == ui->day);
            uint32_t ink = i == ui->day ? SEL_TEXT : INK;
            uint32_t dim = i == ui->day ? SEL_TEXT : MUTED;
            if (!loc->forecast_valid) {
                label(dst, stride, TEXT_X, y + 7, "--", 0, dim, 2);
                continue;
            }
            const WeatherDay *d = &loc->days[i];
            label(dst, stride, TEXT_X, y + 7, d->date + 5, 0, ink, 5);
            label(dst, stride, 110, y + 7, condition(d->weather_code), 0, dim, 20);
            char temp[24];
            snprintf(temp, sizeof temp, "%d", units(d->high_c, ui->units_f));
            label_right(dst, stride, temp, 0, ink, 265, y + 7);
            snprintf(temp, sizeof temp, "%d", units(d->low_c, ui->units_f));
            label_right(dst, stride, temp, 0, dim, 310, y + 7);
        }
    } else {
        const WeatherLocation *loc = &ui->weather.locations[ui->slot];
        label_styled(dst, stride, TEXT_X, BAR_H + 7, loc->used ? loc->name : "No location", 0, INK,
                     20, scheme.caps_rows);
        if (loc->forecast_valid) {
            const WeatherDay *day = &loc->days[ui->day];
            char temp[48];
            snprintf(temp, sizeof temp, "%d%c", units(day->high_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            label(dst, stride, TEXT_X, BAR_H + ROW_H + 6, temp, 2, INK, 5);
            label(dst, stride, TEXT_X + text_width(temp, 1) + 12, BAR_H + ROW_H + 16,
                  condition(day->weather_code), 0, MUTED, 27);
            int y = BAR_H + ROW_H + 48;
            label(dst, stride, TEXT_X, y + 7, day->date, 0, INK, 11);
            snprintf(temp, sizeof temp, "%d%c", units(day->high_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            detail_row(dst, stride, y + ROW_H, TR(HIGH), temp, 0);
            snprintf(temp, sizeof temp, "%d%c", units(day->low_c, ui->units_f),
                     ui->units_f ? 'F' : 'C');
            detail_row(dst, stride, y + ROW_H * 2, TR(LOW), temp, 0);
            if (day->precipitation_pct >= 0) {
                snprintf(temp, sizeof temp, "%d%%", day->precipitation_pct);
                detail_row(dst, stride, y + ROW_H * 3, TR(RAIN_CHANCE), temp, 0);
            }
        } else {
            label(dst, stride, TEXT_X, BAR_H + ROW_H + 16, TR(FORECAST_UNAVAILABLE), 0, MUTED, 32);
        }
        label(dst, stride, TEXT_X, 296, "CENTER FORECAST   BACK", 0, MUTED, 32);
    }
}
