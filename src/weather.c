/* SPDX-License-Identifier: MIT */
#include "weather.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int coordinates(double lat,double lon) {
    return isfinite(lat)&&isfinite(lon)&&lat>=-90&&lat<=90&&lon>=-180&&lon<=180;
}
void weather_init(WeatherApp *app) {if(app) memset(app,0,sizeof(*app));}
int weather_set_location(WeatherApp *app,int slot,const char *name,double lat,double lon) {
    if(!app||slot<0||slot>=Q2_WEATHER_SLOTS||!name||!*name||
       !coordinates(lat,lon))return WEATHER_INVALID;
    size_t len=strlen(name);
    if(len>=Q2_WEATHER_NAME)return WEATHER_INVALID;
    for(size_t i=0;i<len;i++)
        if((unsigned char)name[i]<32||name[i]=='|'||name[i]=='\r'||name[i]=='\n')
            return WEATHER_INVALID;
    WeatherLocation *l=&app->locations[slot];
    if (l->used && fabs(l->latitude - lat) < 0.00001 &&
        fabs(l->longitude - lon) < 0.00001 && !strcmp(l->name, name))
        return WEATHER_OK;
    memset(l,0,sizeof(*l));memcpy(l->name,name,len+1);
    l->latitude=lat;l->longitude=lon;l->used=1;
    return WEATHER_OK;
}
int weather_refresh(WeatherApp *app,const WeatherBackend *backend,int detect_current) {
    if(!app||!backend||!backend->wifi_state||!backend->set_wifi||
       !backend->wait_online||!backend->get_weather||
       (detect_current&&!backend->detect_location))return WEATHER_INVALID;
    int initially_on=backend->wifi_state(backend->ctx);
    if(initially_on<0)return WEATHER_RADIO;
    int ours=0, result=WEATHER_OK;
    if(!initially_on){
        if(backend->set_wifi(backend->ctx,1)) {
            /* A failed enable may still leave the interface powered on. */
            (void)backend->set_wifi(backend->ctx,0);
            return WEATHER_RADIO;
        }
        ours=1;
    }
    if(backend->wait_online(backend->ctx,25)) {result=WEATHER_OFFLINE;goto done;}
    /* Saved Location already has coordinates: fetch its forecast directly
     * before any IP geolocation, even when both location providers are down. */
    for(int pass=0;pass<Q2_WEATHER_SLOTS;pass++) {
        int i = pass == 0 ? 1 : 0;
        if (i == 0 && detect_current) {
            char name[Q2_WEATHER_NAME]={0};
            double lat=0,lon=0;
            if(backend->detect_location(backend->ctx,name,sizeof(name),&lat,&lon)||
               weather_set_location(app,0,name,lat,lon)) {
                /* Keep a previously known home location and its cache. */
                if(result==WEATHER_OK) result=WEATHER_GEO;
            }
        }
        WeatherLocation *l=&app->locations[i];
        if(!l->used)continue;
        int temp=0,humidity=0,code=0;long timestamp=0;
        if(backend->get_weather(backend->ctx,l->latitude,l->longitude,
                                &temp,&humidity,&code,&timestamp)) {
            /* Retain the previous cached weather, do not claim it is fresh. */
            if(result==WEATHER_OK)result=WEATHER_FETCH;
            continue;
        }
        l->temperature_c=temp;l->humidity=humidity;l->weather_code=code;
        l->updated_at=timestamp;l->valid=1;
        if(backend->get_forecast){
            WeatherDay days[Q2_FORECAST_DAYS];
            memset(days, 0, sizeof days);
            if(!backend->get_forecast(backend->ctx,l->latitude,l->longitude,days,Q2_FORECAST_DAYS)){
                memcpy(l->days,days,sizeof(days));
                l->forecast_valid=1;
            } else if(result==WEATHER_OK) result=WEATHER_FETCH;
        }
    }
done:
    /* Restore the power state only if this app switched Wi-Fi on. */
    if(ours&&backend->set_wifi(backend->ctx,0))result=WEATHER_RADIO;
    return result;
}
int weather_on_open(WeatherApp *app,const WeatherBackend *backend) {
    return weather_refresh(app,backend,1);
}
/* Persist both current conditions and the 7-day forecast atomically. The
 * reader also accepts Q2W1, which did not store forecast days. */
int weather_write(const WeatherApp *app, char *out, size_t cap) {
    if (!app || !out || !cap) return -1;
    int count = snprintf(out, cap, "Q2W2\n");
    if (count < 0 || (size_t)count >= cap) return -1;
    for (int i = 0; i < Q2_WEATHER_SLOTS; i++) {
        const WeatherLocation *loc = &app->locations[i];
        int n = snprintf(out + count, cap - (size_t)count,
                         "%d|%d|%s|%.6f|%.6f|%d|%d|%d|%d|%ld\n", i, loc->used,
                         loc->used ? loc->name : "-", loc->latitude, loc->longitude,
                         loc->valid, loc->temperature_c, loc->humidity, loc->weather_code,
                         loc->updated_at);
        if (n < 0 || (size_t)n >= cap - (size_t)count) return -1;
        count += n;
        for (int day = 0; day < Q2_FORECAST_DAYS; day++) {
            const WeatherDay *d = &loc->days[day];
            n = snprintf(out + count, cap - (size_t)count, "%d|%d|%s|%d|%d|%d|%d\n",
                         i, loc->forecast_valid, loc->forecast_valid ? d->date : "-",
                         d->high_c, d->low_c, d->precipitation_pct, d->weather_code);
            if (n < 0 || (size_t)n >= cap - (size_t)count) return -1;
            count += n;
        }
    }
    return count;
}

int weather_read(WeatherApp *app, const char *data) {
    if (!app || !data) return -1;
    int v2 = !strncmp(data, "Q2W2\n", 5);
    if (!v2 && strncmp(data, "Q2W1\n", 5)) return -1;
    WeatherApp next;
    weather_init(&next);
    const char *line = data + 5;
    for (int i = 0; i < Q2_WEATHER_SLOTS; i++) {
        int idx, used, valid, temp, humidity, code, consumed = 0;
        char name[Q2_WEATHER_NAME] = {0};
        double latitude, longitude;
        long updated;
        if (sscanf(line, "%d|%d|%63[^|]|%lf|%lf|%d|%d|%d|%d|%ld%n",
                   &idx, &used, name, &latitude, &longitude, &valid, &temp,
                   &humidity, &code, &updated, &consumed) != 10 ||
            idx != i || consumed <= 0 || (used != 0 && used != 1) ||
            (valid != 0 && valid != 1) || !coordinates(latitude, longitude))
            return -1;
        const char *end = strchr(line, '\n');
        if (!end || end - line != consumed) return -1;
        line = end + 1;
        WeatherLocation *loc = &next.locations[i];
        if (used) {
            if (weather_set_location(&next, i, name, latitude, longitude)) return -1;
            loc->valid = valid;
            loc->temperature_c = temp;
            loc->humidity = humidity;
            loc->weather_code = code;
            loc->updated_at = updated;
        }
        if (!v2) continue;
        int forecast = -1;
        for (int day = 0; day < Q2_FORECAST_DAYS; day++) {
            char date[11] = {0};
            int day_idx, enabled, high, low, rain, wmo;
            consumed = 0;
            if (sscanf(line, "%d|%d|%10[^|]|%d|%d|%d|%d%n", &day_idx, &enabled,
                       date, &high, &low, &rain, &wmo, &consumed) != 7 ||
                day_idx != i || (enabled != 0 && enabled != 1) ||
                consumed <= 0 || (forecast != -1 && forecast != enabled))
                return -1;
            end = strchr(line, '\n');
            if (!end || end - line != consumed) return -1;
            line = end + 1;
            forecast = enabled;
            if (!enabled) continue;
            if (!used || strlen(date) != 10 || date[4] != '-' || date[7] != '-' ||
                rain < -1 || rain > 100)
                return -1;
            WeatherDay *d = &loc->days[day];
            memcpy(d->date, date, sizeof d->date);
            d->high_c = high;
            d->low_c = low;
            d->precipitation_pct = rain;
            d->weather_code = wmo;
        }
        if (used && forecast == 1) loc->forecast_valid = 1;
    }
    if (*line) return -1;
    *app = next;
    return 0;
}
