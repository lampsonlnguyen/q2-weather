/* SPDX-License-Identifier: MIT */
#ifndef WEATHER_LANG_H
#define WEATHER_LANG_H
#include <stddef.h>
typedef enum {
    WX_WEATHER,
    WX_BACK,
    WX_SETTINGS,
    WX_LOCATIONS,
    WX_CURRENT_LOCATION,
    WX_SAVED_LOCATION,
    WX_ADD_CITY,
    WX_FINDING_LOCATION,
    WX_LOADING,
    WX_WAITING_LOCATION,
    WX_CHOOSE_CITY,
    WX_SELECT_LOCATION,
    WX_FORECAST,
    WX_SEARCH_CITY,
    WX_SYSTEM_KEYBOARD,
    WX_TYPE_CITY,
    WX_DONE_SEARCH,
    WX_KEYBOARD_UNAVAILABLE,
    WX_UPDATE_FIRMWARE,
    WX_ENABLE_INPUT,
    WX_CHANGE_CITY,
    WX_UNITS,
    WX_REFRESH,
    WX_CLEAR_CITY,
    WX_SEARCHING,
    WX_UPDATED,
    WX_NOT_FOUND,
    WX_HUMIDITY,
    WX_NO_DATA,
    WX_FORECAST_UNAVAILABLE,
    WX_RAIN_CHANCE,
    WX_HIGH,
    WX_LOW,
    WX_BACK_LOCATIONS,
    WX_CENTER_DETAILS,
    WX_ICON_CREDIT,
    WX_CLEAR,
    WX_PARTLY_CLOUDY,
    WX_CLOUDY,
    WX_FOG,
    WX_DRIZZLE,
    WX_RAIN,
    WX_SNOW,
    WX_SHOWERS,
    WX_SNOW_SHOWERS,
    WX_THUNDERSTORM,
    WX_COUNT
} WeatherText;
const char *weather_text(WeatherText key);
void weather_language_select(int system_language_id);
void weather_language_parse(const char *data, size_t length);
#endif
