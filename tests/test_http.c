/* SPDX-License-Identifier: MIT */
#include "../src/http.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *response;
static char requested_url[1024];
static size_t (*writer)(char *, size_t, size_t, void *);
static void *write_ctx;

void *curl_easy_init(void) { return (void *)1; }
int curl_easy_setopt(void *curl, int option, ...) {
    (void)curl;
    va_list ap;
    va_start(ap, option);
    if (option == 10002)
        snprintf(requested_url, sizeof requested_url, "%s", va_arg(ap, const char *));
    else if (option == 20011)
        writer = va_arg(ap, size_t (*)(char *, size_t, size_t, void *));
    else if (option == 10001)
        write_ctx = va_arg(ap, void *);
    else if (option == 10018 || option == 10065)
        (void)va_arg(ap, const char *);
    else
        (void)va_arg(ap, long);
    va_end(ap);
    return 0;
}
int curl_easy_perform(void *curl) {
    (void)curl;
    if (!response) return 6;
    return writer((char *)response, 1, strlen(response), write_ctx) ==
                   strlen(response)
               ? 0
               : 23;
}
int curl_easy_getinfo(void *curl, int option, ...) {
    (void)curl;
    assert(option == 0x200002);
    va_list ap;
    va_start(ap, option);
    long *status = va_arg(ap, long *);
    *status = 200;
    va_end(ap);
    return 0;
}
void curl_easy_cleanup(void *curl) { (void)curl; }

static const char *complete =
    "{\"latitude\":33.77,\"longitude\":-117.94,"
    "\"current\":{\"time\":\"2026-11-18T22:00\",\"temperature_2m\":21.8,"
    "\"relative_humidity_2m\":63,\"weather_code\":3},"
    "\"daily\":{\"time\":[\"2026-11-18\",\"2026-11-19\",\"2026-11-20\","
    "\"2026-11-21\",\"2026-11-22\",\"2026-11-23\",\"2026-11-24\"],"
    "\"temperature_2m_max\":[24,25,24,23,25,26,24],"
    "\"temperature_2m_min\":[14,14,13,12,13,14,12],"
    "\"precipitation_probability_max\":[null,10,20,15,5,0,0],"
    "\"weather_code\":[3,3,61,61,2,1,0]}}";

int main(void) {
    int temp = 0, humidity = 0, weather_code = 0, highs[7], lows[7], rain[7], codes[7];
    long updated = 0;
    char dates[7][11] = {{0}};
    response = complete;
    assert(weather_request(33.77, -117.94, &temp, &humidity, &weather_code, &updated,
                           dates, highs, lows, rain, codes, 7) == 0);
    assert(temp == 22 && humidity == 63 && weather_code == 3);
    assert(rain[0] == -1 && rain[1] == 10);
    assert(!strcmp(dates[6], "2026-11-24") && highs[3] == 23);
    assert(strstr(requested_url, "precipitation_probability_max"));

    response = "{\"current\":{\"temperature_2m\":18.2,"
               "\"relative_humidity_2m\":65,\"weather_code\":1}}";
    assert(weather_request(33.77, -117.94, &temp, &humidity, &weather_code, &updated,
                           dates, highs, lows, rain, codes, 7) == 1);
    assert(temp == 18 && humidity == 65 && weather_code == 1);
    response = "{\"error\":true,\"reason\":\"invalid response\"}";
    assert(weather_request(33.77, -117.94, &temp, &humidity, &weather_code, &updated,
                           dates, highs, lows, rain, codes, 7) == -1);
    /* Multiple same-name cities must remain distinct. */
    WeatherGeoResult cities[WEATHER_GEO_MAX] = {0};
    const char *ambiguous =
        "{\"results\":["
        "{\"name\":\"Westminster\",\"latitude\":33.759,\"longitude\":-117.987,"
        "\"admin1\":\"California\",\"country\":\"United States\"},"
        "{\"name\":\"Westminster\",\"latitude\":39.836,\"longitude\":-105.037,"
        "\"admin1\":\"Colorado\",\"country\":\"United States\"},"
        "{\"name\":\"Westminster\",\"latitude\":51.498,\"longitude\":-0.133,"
        "\"admin1\":\"England\",\"country\":\"United Kingdom\"}]}";
    assert(weather_geocode_parse(ambiguous, cities) == 3);
    assert(!strcmp(cities[0].region, "California"));
    assert(!strcmp(cities[1].region, "Colorado"));
    assert(!strcmp(cities[2].country, "United Kingdom"));
    assert(cities[0].latitude != cities[1].latitude);
    response = ambiguous;
    assert(weather_geocode_many("Westminster", cities) == 3);
    assert(strstr(requested_url, "count=8"));
    assert(weather_geocode_parse("{\"results\":[]}", cities) == 0);
    assert(weather_geocode_parse("{\"results\":[{\"name\":\"Fake\","
                                 "\"latitude\":900,\"longitude\":0}]}", cities) == -1);
    puts("Open-Meteo parser tests passed");
    return 0;
}
