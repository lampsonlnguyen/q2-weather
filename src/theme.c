/* SPDX-License-Identifier: MIT */
#include "theme.h"

#include <string.h>

static int isspace_ini(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static int matches(const char *a, size_t n, const char *b) {
    size_t i = 0;
    while (i < n && b[i]) {
        char ch = a[i];
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (ch != b[i]) return 0;
        i++;
    }
    return i == n && !b[i];
}
static int number(const char *s, size_t n) {
    if (!n || n > 4) return -1;
    int v = 0;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + s[i] - '0';
    }
    return v;
}

void weather_theme_parse(WeatherTheme *out, const char *data, size_t len, int hour) {
    if (!out) return;
    *out = (WeatherTheme){ 0, 0, 0, 50, 0 };
    if (!data || len > 16384) return;
    int section = 0, style = 0, light = 0;
    int minimal = 0, modern = 0, classic = 9;
    for (size_t p = 0; p < len;) {
        size_t start = p;
        while (p < len && data[p] != '\n' && data[p]) ++p;
        size_t end = p;
        if (p < len && data[p] == '\n') ++p;
        while (start < end && isspace_ini(data[start])) start++;
        while (end > start && isspace_ini(data[end - 1])) end--;
        if (start == end || data[start] == '#' || data[start] == ';') continue;
        if (data[start] == '[' && data[end - 1] == ']') {
            section = matches(data + start + 1, end - start - 2, "IPOD")     ? 1
                      : matches(data + start + 1, end - start - 2, "Q2POD")  ? 2
                      : matches(data + start + 1, end - start - 2, "SYSSET") ? 3
                                                                             : 0;
            continue;
        }
        size_t eq = start;
        while (eq < end && data[eq] != '=') eq++;
        if (eq == end) continue;
        size_t key_end = eq;
        while (key_end > start && isspace_ini(data[key_end - 1])) --key_end;
        size_t val = eq + 1;
        while (val < end && isspace_ini(data[val])) ++val;
        int v = number(data + val, end - val);
        if (v < 0) continue;
        size_t key_len = key_end - start;
        if (section == 1) {
            if (matches(data + start, key_len, "THEME") && v <= 2)
                style = v;
            else if (matches(data + start, key_len, "LIGHT") && v <= 2)
                light = v;
            else if (matches(data + start, key_len, "MINIMAL_ACCENT") && v <= 8)
                minimal = v;
            else if (matches(data + start, key_len, "ACCENT") && v <= 8)
                modern = v;
            else if (matches(data + start, key_len, "CLASSIC_LIGHT_ACCENT") && v <= 9)
                classic = v;
        } else if (section == 2) {
            if (matches(data + start, key_len, "WHEELSENSITIVITY") && v >= 10 && v <= 200 &&
                v % 10 == 0)
                out->wheel = v;
        } else if (section == 3) {
            if ((matches(data + start, key_len, "LANGUAGE") ||
                 matches(data + start, key_len, "LANGID")) &&
                v < 100)
                out->language = v;
        }
    }
    out->style = style;
    out->light = light == 2 ? hour >= 7 && hour < 19 : light;
    out->accent = style == 1 ? classic : style == 2 ? modern : minimal;
}
