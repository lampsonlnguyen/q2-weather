/* SPDX-License-Identifier: MIT */
#include "lang.h"

#include <stdio.h>
#include <string.h>

/* English fallback is always available even when a language pack is missing.
 * System languages are identified by Q2 Pod's numeric SYSSET/LANGUAGE value.
 * Future translators can supply a matching .weather/lang/<number>.lang. */
static const struct {
    const char *key;
    const char *english;
} messages[WX_COUNT] = { { "WEATHER", "Weather" },
                         { "BACK", "Back" },
                         { "SETTINGS", "Settings" },
                         { "LOCATIONS", "Locations" },
                         { "CURRENT_LOCATION", "Current Location" },
                         { "SAVED_LOCATION", "Saved Location" },
                         { "ADD_CITY", "Add saved city" },
                         { "FINDING_LOCATION", "Finding location" },
                         { "LOADING", "Loading weather" },
                         { "WAITING_LOCATION", "Waiting for location" },
                         { "CHOOSE_CITY", "Choose a city to track" },
                         { "SELECT_LOCATION", "Select a location for forecast" },
                         { "FORECAST", "7-Day Forecast" },
                         { "SEARCH_CITY", "Search city" },
                         { "SYSTEM_KEYBOARD", "System keyboard" },
                         { "TYPE_CITY", "Type city with Q2 Pod" },
                         { "DONE_SEARCH", "Press Done to search" },
                         { "KEYBOARD_UNAVAILABLE", "Native keyboard unavailable" },
                         { "UPDATE_FIRMWARE", "Update Q2 Pod firmware" },
                         { "ENABLE_INPUT", "To enable system text input" },
                         { "CHANGE_CITY", "Change saved city" },
                         { "UNITS", "Units" },
                         { "REFRESH", "Refresh weather" },
                         { "CLEAR_CITY", "Clear saved city" },
                         { "SEARCHING", "Searching city" },
                         { "UPDATED", "Saved city updated" },
                         { "NOT_FOUND", "City not found" },
                         { "HUMIDITY", "Humidity" },
                         { "NO_DATA", "No data" },
                         { "FORECAST_UNAVAILABLE", "Forecast unavailable" },
                         { "RAIN_CHANCE", "Rain chance" },
                         { "HIGH", "High" },
                         { "LOW", "Low" },
                         { "BACK_LOCATIONS", "Back to locations" },
                         { "CENTER_DETAILS", "Center for details" },
                         { "ICON_CREDIT", "Icons: Tomorrow.io" },
                         { "CLEAR", "Clear" },
                         { "PARTLY_CLOUDY", "Partly cloudy" },
                         { "CLOUDY", "Cloudy" },
                         { "FOG", "Fog" },
                         { "DRIZZLE", "Drizzle" },
                         { "RAIN", "Rain" },
                         { "SNOW", "Snow" },
                         { "SHOWERS", "Showers" },
                         { "SNOW_SHOWERS", "Snow showers" },
                         { "THUNDERSTORM", "Thunderstorm" } };
static char translated[WX_COUNT][96];

const char *weather_text(WeatherText key) {
    if ((unsigned)key >= WX_COUNT) return "";
    return translated[key][0] ? translated[key] : messages[key].english;
}

void weather_language_parse(const char *data, size_t length) {
    memset(translated, 0, sizeof translated);
    if (!data || length > 16384) return;
    size_t pos = 0;
    while (pos < length) {
        size_t begin = pos;
        while (pos < length && data[pos] != '\n' && data[pos]) pos++;
        size_t end = pos;
        if (pos < length && data[pos] == '\n') pos++;
        while (end > begin && (data[end - 1] == '\r' || data[end - 1] == ' ')) end--;
        if (begin == end || data[begin] == '#' || data[begin] == ';') continue;
        size_t eq = begin;
        while (eq < end && data[eq] != '=') eq++;
        if (eq == end) continue;
        for (unsigned k = 0; k < WX_COUNT; ++k) {
            size_t key_len = strlen(messages[k].key);
            if (eq - begin != key_len || strncmp(data + begin, messages[k].key, key_len)) continue;
            size_t size = end - eq - 1;
            if (!size || size >= sizeof translated[k]) break;
            int safe = 1;
            for (size_t i = eq + 1; i < end; ++i)
                if ((unsigned char)data[i] < 32 && data[i] != '\t') safe = 0;
            if (safe) {
                memcpy(translated[k], data + eq + 1, size);
                translated[k][size] = 0;
            }
            break;
        }
    }
}

void weather_language_select(int system_language_id) {
    memset(translated, 0, sizeof translated);
    if (system_language_id < 0 || system_language_id > 99) return;
    char path[96], data[16384];
    snprintf(path, sizeof path, "/mnt/mmc/.weather/lang/%d.lang", system_language_id);
    FILE *file = fopen(path, "rb");
    if (!file) return;
    size_t length = fread(data, 1, sizeof data, file);
    fclose(file);
    weather_language_parse(data, length);
}
