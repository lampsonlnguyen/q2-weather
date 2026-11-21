/* SPDX-License-Identifier: MIT */
#include "dashboard.h"

#include <stdio.h>
#include <string.h>

/* Ten keys on row one, nine on row two, seven on row three.
 * The bottom row has ABC/123, Space, Clear, Backspace and Search. */
static const char letter_keys[] = "QWERTYUIOPASDFGHJKLZXCVBNM";
static const char number_keys[] = "1234567890-./'@#&()+*?=!:,";
#define LETTER_KEYS 26
#define KEY_COUNT 31

static void keyboard_action(Dashboard *ui) {
    int index = ui->keyboard_index;
    if (index == 26) {
        ui->keyboard_numbers = !ui->keyboard_numbers;
        return;
    }
    if (index == 30) {
        if (ui->city_length) {
            ui->search_request = 1;
            ui->match_count = 0;
            ui->match_selected = 0;
            ui->search_result = 1;
            ui->page = 2;
            ui->keyboard_index = 0;
        }
    } else if (index == 28) {
        ui->city_length = 0;
        ui->city_query[0] = 0;
    } else if (index == 29) {
        if (ui->city_length) ui->city_query[--ui->city_length] = 0;
    } else if (ui->city_length < (int)sizeof ui->city_query - 1) {
        char ch = index == 27            ? ' '
                  : ui->keyboard_numbers ? number_keys[index]
                                         : letter_keys[index];
        ui->city_query[ui->city_length++] = ch;
        ui->city_query[ui->city_length] = 0;
    }
}

static void open_location(Dashboard *ui) {
    ui->day = 0;
    ui->wheel_rem = 0;
    if (!ui->weather.locations[ui->slot].used) {
        ui->city_target = ui->slot;
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
    } else if (ui->page == 5) {
        ui->page = 3;
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
        ui->city_target = 1;
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
    case 4:
        ui->city_target = 0;
        ui->page = 3;
        ui->keyboard_index = 0;
        break;
    default:
        break;
    }
}

void dashboard_init(Dashboard *ui) {
    memset(ui, 0, sizeof *ui);
    ui->redraw = 1;
    ui->wheel_percent = 50;
    ui->city_target = 1;
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
        int movement = -wheel;
        /* A direction change starts a fresh detent instead of cancelling
         * the previous direction's unconsumed remainder. */
        if ((movement > 0 && ui->wheel_rem < 0) || (movement < 0 && ui->wheel_rem > 0))
            ui->wheel_rem = 0;
        ui->wheel_rem += movement;
        int changed = 0;
        int *selection = ui->page == 5                    ? &ui->match_selected
                         : ui->page == 0                  ? &ui->home_selected
                         : ui->page == 1 || ui->page == 4 ? &ui->day
                                                          : &ui->keyboard_index;
        int max = ui->page == 5   ? ui->match_count - 1
                  : ui->page == 0 ? 2
                  : ui->page == 2 ? 4
                  : ui->page == 3 ? KEY_COUNT - 1
                                  : Q2_FORECAST_DAYS - 1;
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
            if (ui->page == 5) {
                if (ui->match_count > 0) {
                    ui->save_request = 1;
                    ui->search_result = 1;
                    ui->page = 2;
                    ui->keyboard_index = 0;
                }
            } else if (ui->page == 3)
                keyboard_action(ui);
            else if (ui->page == 2)
                settings_action(ui);
            else if (ui->page == 0) {
                if (ui->home_selected == 2) {
                    ui->page = 2;
                    ui->keyboard_index = 0;
                } else {
                    ui->slot = ui->home_selected;
                    open_location(ui);
                }
            } else if (ui->page == 1) {
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
        } else if (ui->page == 5) {
            int row = (y - 90) / 64;
            if (y < 90 || y >= 282 || x < 34 || x >= 341 || row < 0 || row >= 3) return 0;
            int start = (ui->match_selected / 3) * 3;
            int idx = start + row;
            if (idx >= ui->match_count) return 0;
            ui->match_selected = idx;
            ui->save_request = 1;
            ui->search_result = 1;
            ui->page = 2;
            ui->keyboard_index = 0;
        } else if (ui->page == 3) {
            int idx = -1;
            if (y >= 115 && y < 149 && x >= 32 && x < 342) {
                idx = (x - 32) / 31;
            } else if (y >= 154 && y < 188 && x >= 47 && x < 326) {
                idx = 10 + (x - 47) / 31;
            } else if (y >= 193 && y < 227 && x >= 78 && x < 295) {
                idx = 19 + (x - 78) / 31;
            } else if (y >= 238 && y < 275 && x >= 34 && x < 342) {
                idx = 26 + (x - 34) / 62;
            }
            if (idx < 0 || idx >= KEY_COUNT) return 0;
            ui->keyboard_index = idx;
            keyboard_action(ui);
        } else if (ui->page == 2) {
            int idx = (y - 26) / 32;
            if (y < 26 || idx < 0 || idx >= 5) return 0;
            ui->keyboard_index = idx;
            settings_action(ui);
        } else if (ui->page == 0 && y >= 26 && y < 186) {
            /* Locations list: two 64 px location rows, then the 32 px
             * Settings row (dashboard_render.c page 0). */
            if (y >= 154)
                ui->home_selected = 2;
            else if (y >= 90)
                ui->home_selected = 1;
            else
                ui->home_selected = 0;
            if (ui->home_selected == 2) {
                ui->page = 2;
                ui->keyboard_index = 0;
            } else {
                ui->slot = ui->home_selected;
                open_location(ui);
            }
        } else if (ui->page == 1 && y >= 90 && y < 314) {
            int idx = (y - 90) / 32;
            if (idx < 0 || idx >= Q2_FORECAST_DAYS) return 0;
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
