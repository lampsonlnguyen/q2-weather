/* SPDX-License-Identifier: MIT */
#include "../src/weather.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {int on,enabled,disabled,fail_online,fail_geo,fail_weather,requests,saved_first;} Fake;
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
 Fake*f=p;assert(a>=-90&&a<=90&&b>=-180&&b<=180);if(!f->requests && a==11.94 && b==108.44)f->saved_first=1;f->requests++;
 if(f->fail_weather)return -1;
 *t=22;*h=65;*c=3;*stamp=1795000000;return 0;
}
static int forecast_days(void*p,double lat,double lon,WeatherDay *days,int count){
 Fake*f=p;assert(count==Q2_FORECAST_DAYS);assert(lat>=-90&&lon<=180);
 if(f->fail_weather)return -1;
 for(int i=0;i<count;i++){
  snprintf(days[i].date,sizeof(days[i].date),"2026-11-%02d",18+i);
  days[i].high_c=23+i;days[i].low_c=12+i;
  days[i].precipitation_pct=15;days[i].weather_code=3;
 }
 return 0;
}
static WeatherBackend backend(Fake*f){
 WeatherBackend b={f,state,power,online,geo,forecast,forecast_days};return b;
}
int main(void){
 WeatherApp app;weather_init(&app);
 assert(weather_set_location(&app,1,"Da Lat",11.94,108.44)==0);
 Fake f={0};WeatherBackend b=backend(&f);
 assert(weather_on_open(&app,&b)==0);
 assert(f.enabled==1&&f.disabled==1&&!f.on&&f.requests==2);
 assert(f.saved_first); /* Saved coordinates do not depend on IP detection. */
 assert(strcmp(app.locations[0].name,"Approximate City")==0);
 assert(app.locations[0].valid&&app.locations[1].temperature_c==22);
 assert(app.locations[0].forecast_valid&&app.locations[1].forecast_valid);
 assert(weather_set_location(&app,0,"Approximate City",33.7700001,-117.94)==WEATHER_OK);
 assert(app.locations[0].valid&&app.locations[0].forecast_valid);
 assert(app.locations[1].days[6].high_c==29);
 char buf[1024];int n=weather_write(&app,buf,sizeof(buf));assert(n>0);
 WeatherApp restored;assert(weather_read(&restored,buf)==0);
 assert(restored.locations[0].valid&&strcmp(restored.locations[1].name,"Da Lat")==0);
 assert(restored.locations[1].forecast_valid);
 assert(restored.locations[1].days[6].high_c==29);
 f=(Fake){.on=1};b=backend(&f);
 assert(weather_refresh(&restored,&b,0)==0);
 assert(f.enabled==0&&f.disabled==0&&f.on==1);
 f=(Fake){.fail_online=1};b=backend(&f);
 assert(weather_refresh(&app,&b,0)==WEATHER_OFFLINE);
 assert(f.enabled==1&&f.disabled==1&&!f.on);
 f=(Fake){.fail_geo=1};b=backend(&f);
 assert(weather_refresh(&app,&b,1)==WEATHER_GEO);
 assert(f.enabled==1&&f.disabled==1);
 assert(f.requests==2); /* Both previously known cities still refresh. */
 assert(f.saved_first); /* Saved city fetched before failed IP geolocation. */
 f=(Fake){.fail_weather=1};b=backend(&f);
 assert(weather_refresh(&app,&b,0)==WEATHER_FETCH);
 assert(f.enabled==1&&f.disabled==1);
 assert(weather_set_location(&app,1,"oops",100,0)==WEATHER_INVALID);
 assert(weather_set_location(&app,2,"Hanoi",21.03,105.85)==WEATHER_INVALID);
 puts("weather state and Wi-Fi ownership tests passed");
 return 0;
}
