/* SPDX-License-Identifier: MIT */
#include "art.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define ART_PATH "/tmp/q2-weather.art"
#define SOURCE_PATH "/mnt/mmc/.weather/icons"

const char *weather_art_icon(int wmo) {
    if (wmo == 0) return "sunny";
    if (wmo == 1 || wmo == 2) return "partly";
    if (wmo == 3) return "overcast";
    if (wmo == 45 || wmo == 48) return "fog";
    if (wmo >= 51 && wmo <= 57) return "drizzle";
    if (wmo >= 61 && wmo <= 67) return "rain";
    if (wmo >= 71 && wmo <= 77) return "snow";
    if (wmo >= 80 && wmo <= 82) return "showers";
    if (wmo >= 85 && wmo <= 86) return "snowshowers";
    if (wmo >= 95 && wmo <= 99) return "thunder";
    return "cloudy";
}

/* The condition in words, as the rows show it (Open-Meteo's WMO codes). */
const char *weather_condition(int wmo) {
    if (wmo == 0) return "Clear";
    if (wmo == 1) return "Mostly clear";
    if (wmo == 2) return "Partly cloudy";
    if (wmo == 3) return "Overcast";
    if (wmo == 45 || wmo == 48) return "Fog";
    if (wmo >= 51 && wmo <= 57) return "Drizzle";
    if (wmo >= 61 && wmo <= 67) return "Rain";
    if (wmo >= 71 && wmo <= 77) return "Snow";
    if (wmo >= 80 && wmo <= 82) return "Showers";
    if (wmo >= 85 && wmo <= 86) return "Snow showers";
    if (wmo >= 95) return "Thunderstorms";
    return "Cloudy";
}

/* Same FNV-1a 32 hash as Q2 Pod patch/addon.c art_url:
 * key = FNV(uri + "\t" + context). */
unsigned weather_art_key(const char *uri, const char *context) {
    uint32_t hash = 2166136261u;
    for (const char *s = uri; *s; ++s) {
        hash ^= (unsigned char)*s;
        hash *= 16777619u;
    }
    hash ^= (unsigned char)'\t';
    hash *= 16777619u;
    for (const char *s = context; *s; ++s) {
        hash ^= (unsigned char)*s;
        hash *= 16777619u;
    }
    return hash;
}

void weather_art_init(void) {
#ifndef HOST
    (void)mkdir(ART_PATH, 0755);
#endif
}

/* The row's cover, copied whole from the card's icon into Q2 Pod's art folder and renamed into
 * place, so the firmware never reads half a file. The key covers the condition (the context
 * carries its code), so a file already there is the right one and isn't copied again. */
void weather_art_row(const char *uri, const char *context, int wmo) {
#ifndef HOST
    char source[160], target[160], temp[176], buffer[4096];
    snprintf(source, sizeof source, SOURCE_PATH "/%s.jpg", weather_art_icon(wmo));
    snprintf(target, sizeof target, ART_PATH "/%08x.jpg", weather_art_key(uri, context));
    if (!access(target, R_OK)) return;
    snprintf(temp, sizeof temp, "%s.tmp", target);
    FILE *in = fopen(source, "rb");
    if (!in) return;
    FILE *out = fopen(temp, "wb");
    if (!out) {
        fclose(in);
        return;
    }
    size_t count;
    int ok = 1;
    while ((count = fread(buffer, 1, sizeof buffer, in)) > 0)
        if (fwrite(buffer, 1, count, out) != count) ok = 0;
    if (ferror(in)) ok = 0;
    if (fclose(out)) ok = 0;
    fclose(in);
    if (!ok || rename(temp, target)) unlink(temp);
#else
    (void)uri;
    (void)context;
    (void)wmo;
#endif
}

void weather_art_cleanup(void) {
    /* Q2 Pod unloads the covers when the level closes. The small /tmp files
     * are reused across opens and cleared on reboot. */
}
