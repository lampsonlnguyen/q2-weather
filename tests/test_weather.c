/* SPDX-License-Identifier: MIT */
#include "../src/weather.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {int on,enabled,disabled,fail_online,fail_geo,fail_weather,requests;} Fake;
static int state(void*p){return ((Fake*)p)->on;}
static int power(void*p,int value){
 Fake*f=p;f->on=value;if(value)f->enabled++;else f->disabled++;return 0;
}
static int online(void*p,int seconds){assert(seconds==25);return ((Fake*)p)->fail_online;}
static int geo(void*p,char*n,size_t c,double*a,double*b){
 if(((Fake*)p)->fail_geo)return -1;
 snprintf(n,c,"Approximate City");*a=33.77;*b=-117.94;return 0;
}
static int forecast(void*p,double a,double b,int*t,int*h,int*c,long*stamp){
 Fake*f=p;assert(a>=-90&&a<=90&&b>=-180&&b<=180);f->requests++;
 if(f->fail_weather)return -1;
 *t=22;*h=65;*c=3;*stamp=1795000000;return 0;
}
static WeatherBackend backend(Fake*f){
 WeatherBackend b={f,state,power,online,geo,forecast};return b;
}
int main(void){
 WeatherApp app;weather_init(&app);
 assert(weather_set_location(&app,1,"Da Lat",11.94,108.44)==0);
 Fake f={0};WeatherBackend b=backend(&f);
 assert(weather_on_open(&app,&b)==0);
 assert(f.enabled==1&&f.disabled==1&&!f.on&&f.requests==2);
 assert(strcmp(app.locations[0].name,"Approximate City")==0);
 assert(app.locations[0].valid&&app.locations[1].temperature_c==22);
 char buf[1024];int n=weather_write(&app,buf,sizeof(buf));assert(n>0);
 WeatherApp restored;assert(weather_read(&restored,buf)==0);
 assert(restored.locations[0].valid&&strcmp(restored.locations[1].name,"Da Lat")==0);
 f=(Fake){.on=1};b=backend(&f);
 assert(weather_refresh(&restored,&b,0)==0);
 assert(f.enabled==0&&f.disabled==0&&f.on==1);
 f=(Fake){.fail_online=1};b=backend(&f);
 assert(weather_refresh(&app,&b,0)==WEATHER_OFFLINE);
 assert(f.enabled==1&&f.disabled==1&&!f.on);
 f=(Fake){.fail_geo=1};b=backend(&f);
 assert(weather_refresh(&app,&b,1)==WEATHER_GEO);
 assert(f.enabled==1&&f.disabled==1);
 f=(Fake){.fail_weather=1};b=backend(&f);
 assert(weather_refresh(&app,&b,0)==WEATHER_FETCH);
 assert(f.enabled==1&&f.disabled==1);
 assert(weather_set_location(&app,1,"oops",100,0)==WEATHER_INVALID);
 assert(weather_set_location(&app,2,"Hanoi",21.03,105.85)==WEATHER_INVALID);
 puts("weather state and Wi-Fi ownership tests passed");
 return 0;
}
