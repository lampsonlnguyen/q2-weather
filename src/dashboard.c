/* SPDX-License-Identifier: MIT */
#include "dashboard.h"

#include <stdio.h>
#include <string.h>

void dashboard_init(Dashboard *ui) {
    memset(ui, 0, sizeof *ui);
    ui->redraw = 1;
}

int dashboard_input(Dashboard *ui, const char *event) {
    if (!ui || !event) return 0;
    int wheel;
    if (sscanf(event, "w %d", &wheel) == 1) {
        /* A single Q2 wheel turn reports about 200 travel units. */
        ui->wheel_rem += wheel;
        int changed = 0;
        while (ui->wheel_rem >= 28) {
            ui->wheel_rem -= 28;
            if (ui->day < Q2_FORECAST_DAYS - 1) ui->day++, changed = 1;
        }
        while (ui->wheel_rem <= -28) {
            ui->wheel_rem += 28;
            if (ui->day > 0) ui->day--, changed = 1;
        }
        if (changed) ui->redraw = 1;
        return changed;
    }
    int code;
    if (sscanf(event, "k d %d", &code) == 1) {
        if (code == 170) {
            if (ui->detailed)
                ui->detailed = 0;
            else
                ui->quit = 1;
        } else if (code == 218) {
            ui->detailed = !ui->detailed;
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
            if (ui->detailed)
                ui->detailed = 0;
            else
                ui->quit = 1;
        } else if (y >= 55 && y < 183) {
            ui->slot = y >= 125 ? 1 : 0;
            ui->day = 0;
            ui->detailed = 0;
        } else if (y >= 200 && y <= 280) {
            int idx = (x - 14) / 50;
            if (idx >= 0 && idx < Q2_FORECAST_DAYS) {
                ui->day = idx;
                ui->detailed = 1;
            }
        } else {
            return 0;
        }
        ui->redraw = 1;
        return 1;
    }
    return 0;
}
