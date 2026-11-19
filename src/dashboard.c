/* SPDX-License-Identifier: MIT */
#include "dashboard.h"

#include <stdio.h>
#include <string.h>

/* QWERTY rows: 10, 9, 7 letters; Space / Delete / Search. */
static const char keyboard[] = "QWERTYUIOPASDFGHJKLZXCVBNM_<>"; 
#define KEY_COUNT ((int)sizeof keyboard - 1)

static void keyboard_action(Dashboard *ui) {
    char ch = keyboard[ui->keyboard_index];
    if (ch == '>') {
        if (ui->city_length) {
            ui->search_request = 1;
            ui->search_result = 1;
            ui->page = 2;
            ui->keyboard_index = 0;
        }
    } else if (ch == '<') {
        if (ui->city_length) ui->city_query[--ui->city_length] = 0;
    } else if (ui->city_length < (int)sizeof ui->city_query - 1) {
        ui->city_query[ui->city_length++] = ch == '_' ? ' ' : ch;
        ui->city_query[ui->city_length] = 0;
    }
}


static void open_location(Dashboard *ui) {
    ui->day = 0;
    ui->wheel_rem = 0;
    if (ui->slot == 1 && !ui->weather.locations[1].used) {
        ui->page = 3;
        ui->keyboard_index = 0;
    } else {
        ui->page = 1;
    }
    ui->detailed = 0;
}

static void back(Dashboard *ui) {
    if (ui->page == 3) {
        ui->page = 2;
        ui->keyboard_index = 0;
    } else if (ui->page == 4) {
        ui->page = 1;
        ui->detailed = 0;
    } else if (ui->page == 1 || ui->page == 2) {
        ui->page = 0;
    } else {
        ui->quit = 1;
    }
    ui->wheel_rem = 0;
}

static void settings_action(Dashboard *ui) {
    switch (ui->keyboard_index) {
    case 0:
        ui->page = 3;
        ui->keyboard_index = 0;
        break;
    case 1:
        ui->units_f = !ui->units_f;
        break;
    case 2:
        ui->refresh_request = 1;
        break;
    case 3:
        ui->clear_request = 1;
        break;
    default:
        break;
    }
}

void dashboard_init(Dashboard *ui) {
    memset(ui, 0, sizeof *ui);
    ui->redraw = 1;
    ui->wheel_percent = 50;
}

int dashboard_input(Dashboard *ui, const char *event) {
    if (!ui || !event) return 0;
    /* Native Q2 firmware IM sends 's <UTF-8 city>' when Done is pressed. */
    if (ui->page == 3 && event[0] == 's' && event[1] == ' ') {
        const char *city = event + 2;
        size_t n = strlen(city);
        if (n > 0 && n < sizeof ui->city_query) {
            memcpy(ui->city_query, city, n + 1);
            ui->city_length = (int)n;
            ui->search_request = 1;
            ui->search_result = 1;
        }
        ui->page = 2;
        ui->keyboard_index = 0;
        ui->redraw = 1;
        return 1;
    }
    int wheel;
    if (sscanf(event, "w %d", &wheel) == 1) {
        /* Q2 full-screen input sign is opposite the Library list. */
        ui->wheel_rem -= wheel;
        int changed = 0;
        int *selection = ui->page == 0 ? &ui->slot :
                         ui->page == 1 || ui->page == 4 ? &ui->day :
                         &ui->keyboard_index;
        int max = ui->page == 0 ? 1 :
                  ui->page == 2 ? 3 :
                  ui->page == 3 ? KEY_COUNT - 1 :
                  Q2_FORECAST_DAYS - 1;
        int step = 1400 / (ui->wheel_percent >= 10 ? ui->wheel_percent : 50);
        if (step < 8) step = 8;
        if (step > 140) step = 140;
        while (ui->wheel_rem >= step) {
            ui->wheel_rem -= step;
            if (*selection < max) (*selection)++, changed = 1;
        }
        while (ui->wheel_rem <= -step) {
            ui->wheel_rem += step;
            if (*selection > 0) (*selection)--, changed = 1;
        }
        if (changed) ui->redraw = 1;
        return changed;
    }
    int code;
    if (sscanf(event, "k d %d", &code) == 1) {
        if (code == 170) {
            back(ui);
        } else if (code == 218) {
            if (ui->page == 3)
                keyboard_action(ui);
            else if (ui->page == 2)
                settings_action(ui);
            else if (ui->page == 0)
                open_location(ui);
            else if (ui->page == 1) {
                ui->page = 4;
                ui->detailed = 1;
            } else if (ui->page == 4) {
                ui->page = 1;
                ui->detailed = 0;
            }
        } else {
            return 0;
        }
        ui->redraw = 1;
        return 1;
    }
    char op;
    int x, y;
    if (sscanf(event, "t %c %d %d", &op, &x, &y) == 3 && op == 'u') {
        if (x < 0 || x >= DASH_WIDTH || y < 0 || y >= DASH_HEIGHT) return 0;
        if (y < 42 && x < 85) {
            back(ui);
        } else if (ui->page == 3) {
            int idx = -1;
            if (y >= 115 && y < 149 && x >= 32 && x < 342) {
                idx = (x - 32) / 31;
            } else if (y >= 154 && y < 188 && x >= 47 && x < 326) {
                idx = 10 + (x - 47) / 31;
            } else if (y >= 193 && y < 227 && x >= 78 && x < 295) {
                idx = 19 + (x - 78) / 31;
            } else if (y >= 238 && y < 275 && x >= 47 && x < 326) {
                idx = 26 + (x - 47) / 93;
            }
            if (idx < 0 || idx >= KEY_COUNT) return 0;
            ui->keyboard_index = idx;
            keyboard_action(ui);
        } else if (ui->page == 2) {
            int idx = (y - 76) / 48;
            if (y < 76 || idx < 0 || idx >= 4) return 0;
            ui->keyboard_index = idx;
            settings_action(ui);
        } else if (ui->page == 0 && y < 42 && x > 250) {
            ui->page = 2;
            ui->keyboard_index = 0;
        } else if (ui->page == 0 && y >= 65 && y < 265) {
            if (y >= 156 && y < 255) ui->slot = 1;
            else if (y >= 65 && y < 155) ui->slot = 0;
            else return 0;
            open_location(ui);
        } else if (ui->page == 1 && y >= 209 && y <= 283) {
            int idx = (x - 31) / 45;
            if (x < 31 || idx < 0 || idx >= Q2_FORECAST_DAYS) return 0;
            ui->day = idx;
            ui->page = 4;
            ui->detailed = 1;
        } else {
            return 0;
        }
        ui->redraw = 1;
        return 1;
    }
    return 0;
}
