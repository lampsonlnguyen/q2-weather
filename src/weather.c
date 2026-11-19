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
        if(backend->set_wifi(backend->ctx,1))return WEATHER_RADIO;
        ours=1;
    }
    if(backend->wait_online(backend->ctx,25)) {result=WEATHER_OFFLINE;goto done;}
    if(detect_current) {
        char name[Q2_WEATHER_NAME]={0};
        double lat=0,lon=0;
        if(backend->detect_location(backend->ctx,name,sizeof(name),&lat,&lon)||
           weather_set_location(app,0,name,lat,lon)) {
            result=WEATHER_GEO;goto done;
        }
    }
    for(int i=0;i<Q2_WEATHER_SLOTS;i++) {
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
            WeatherDay days[Q2_FORECAST_DAYS]={{0}};
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
int weather_write(const WeatherApp *app,char *out,size_t cap){
    if(!app||!out||!cap)return -1;
    int count=snprintf(out,cap,"Q2W1\n");
    if(count<0||(size_t)count>=cap)return -1;
    for(int i=0;i<Q2_WEATHER_SLOTS;i++) {
        const WeatherLocation *l=&app->locations[i];
        int n=snprintf(out+count,cap-(size_t)count,
            "%d|%d|%s|%.6f|%.6f|%d|%d|%d|%d|%ld\n",i,l->used,l->used?l->name:"-",
            l->latitude,l->longitude,l->valid,l->temperature_c,l->humidity,
            l->weather_code,l->updated_at);
        if(n<0||(size_t)n>=cap-(size_t)count)return -1;
        count+=n;
    }
    return count;
}
int weather_read(WeatherApp *app,const char *data) {
    if(!app||!data||strncmp(data,"Q2W1\n",5))return -1;
    WeatherApp next;weather_init(&next);
    const char *p=data+5;
    for(int i=0;i<Q2_WEATHER_SLOTS;i++) {
        int idx,used,valid,temp,humid,code,offset=0;
        long time;char name[Q2_WEATHER_NAME]={0};
        double lat,lon;
        /* Require an intact fixed-field record; empty slots use a placeholder. */
        if(sscanf(p,"%d|%d|%63[^|]|%lf|%lf|%d|%d|%d|%d|%ld%n",
                  &idx,&used,name,&lat,&lon,&valid,&temp,&humid,&code,&time,&offset)!=10||
           idx!=i||!offset||(used!=0&&used!=1)||!coordinates(lat,lon)||
           (valid!=0&&valid!=1))return -1;
        if(used) {
            if(weather_set_location(&next,i,name,lat,lon))return -1;
            WeatherLocation *l=&next.locations[i];
            l->valid=valid;l->temperature_c=temp;l->humidity=humid;
            l->weather_code=code;l->updated_at=time;
        }
        const char *newline=strchr(p,'\n');
        if(!newline||newline-p!=offset)return -1;
        p=newline+1;
    }
    if(*p)return -1;
    *app=next;return 0;
}
