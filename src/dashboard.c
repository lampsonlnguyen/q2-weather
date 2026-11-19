/* SPDX-License-Identifier: MIT */
#include "dashboard.h"

#include <stdio.h>
#include <string.h>

static const char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _<+";

static void select_key(Dashboard *ui) {
    char key = keys[ui->keyboard_index];
    if (key == '+') {
        if (ui->city_length) {
            ui->search_request = 1;
            ui->search_result = 1;
            ui->page = 2;
        }
    } else if (key == '<') {
        if (ui->city_length) ui->city_query[--ui->city_length] = 0;
    } else if (ui->city_length < (int)sizeof ui->city_query - 1) {
        ui->city_query[ui->city_length++] = key;
        ui->city_query[ui->city_length] = 0;
    }
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
}

int dashboard_input(Dashboard *ui, const char *event) {
    if (!ui || !event) return 0;
    int wheel;
    if (sscanf(event, "w %d", &wheel) == 1) {
        ui->wheel_rem += wheel;
        int changed = 0;
        int *selection = ui->page >= 2 ? &ui->keyboard_index : &ui->day;
        int max = ui->page == 3 ? (int)strlen(keys) - 1 :
                  ui->page == 2 ? 3 : Q2_FORECAST_DAYS - 1;
        while (ui->wheel_rem >= 28) {
            ui->wheel_rem -= 28;
            if (*selection < max) (*selection)++, changed = 1;
        }
        while (ui->wheel_rem <= -28) {
            ui->wheel_rem += 28;
            if (*selection > 0) (*selection)--, changed = 1;
        }
        if (changed) ui->redraw = 1;
        return changed;
    }
    int code;
    if (sscanf(event, "k d %d", &code) == 1) {
        if (code == 170) {
            if (ui->page == 3) {
                ui->page = 2;
                ui->keyboard_index = 0;
            } else if (ui->page == 2 || ui->page == 1) {
                ui->page = 0;
                ui->detailed = 0;
            } else {
                ui->quit = 1;
            }
        } else if (code == 218) {
            if (ui->page == 3)
                select_key(ui);
            else if (ui->page == 2)
                settings_action(ui);
            else {
                ui->page = ui->page == 0 ? 1 : 0;
                ui->detailed = ui->page == 1;
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
            if (ui->page == 3) {
                ui->page = 2;
                ui->keyboard_index = 0;
            } else if (ui->page == 2 || ui->page == 1) {
                ui->page = 0;
                ui->detailed = 0;
            } else {
                ui->quit = 1;
            }
        } else if (ui->page == 3) {
            int col = (x - 14) / 58;
            int row = (y - 80) / 32;
            int idx = row * 6 + col;
            if (x < 14 || col < 0 || col >= 6 || y < 80 || row < 0 ||
                idx >= (int)strlen(keys))
                return 0;
            ui->keyboard_index = idx;
            select_key(ui);
        } else if (ui->page == 2) {
            int idx = (y - 76) / 48;
            if (y < 76 || idx < 0 || idx >= 4) return 0;
            ui->keyboard_index = idx;
            settings_action(ui);
        } else if (ui->page == 0 && y < 42 && x > 250) {
            ui->page = 2;
            ui->keyboard_index = 0;
        } else if (ui->page == 0 && y >= 55 && y < 183) {
            ui->slot = y >= 125 ? 1 : 0;
            ui->day = 0;
            if (ui->slot && !ui->weather.locations[1].used) {
                ui->page = 3;
                ui->keyboard_index = 0;
            }
        } else if (ui->page <= 1 && y >= 200 && y <= 280) {
            int idx = (x - 14) / 50;
            if (x < 14 || idx < 0 || idx >= Q2_FORECAST_DAYS) return 0;
            ui->day = idx;
            ui->page = 1;
            ui->detailed = 1;
        } else {
            return 0;
        }
        ui->redraw = 1;
        return 1;
    }
    return 0;
}
