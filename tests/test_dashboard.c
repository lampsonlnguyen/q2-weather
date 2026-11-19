/* SPDX-License-Identifier: MIT */
#include "../src/dashboard.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Dashboard ui;
    dashboard_init(&ui);
    assert(ui.day == 0 && ui.slot == 0 && ui.redraw == 1 && !ui.quit);
    ui.redraw = 0;
    assert(dashboard_input(&ui, "w 55") == 1);
    assert(ui.day == 1 && ui.redraw);
    for (int i = 0; i < 100; i++) (void)dashboard_input(&ui, "w 200");
    assert(ui.day == Q2_FORECAST_DAYS - 1);
    for (int i = 0; i < 100; i++) (void)dashboard_input(&ui, "w -200");
    assert(ui.day == 0);
    assert(dashboard_input(&ui, "t u 50 147") == 1);
    assert(ui.slot == 1);
    assert(dashboard_input(&ui, "t u 140 236") == 1);
    assert(ui.day == 2 && ui.detailed);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(!ui.detailed && !ui.quit);
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(ui.detailed);
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(!ui.detailed);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.quit);
    puts("Q2 dashboard wheel, touch and back navigation passed");
    return 0;
}
