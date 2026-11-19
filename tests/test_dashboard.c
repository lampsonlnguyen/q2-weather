/* SPDX-License-Identifier: MIT */
#include "../src/dashboard.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Dashboard ui;
    dashboard_init(&ui);
    assert(ui.page == 0 && ui.slot == 0 && ui.redraw);

    /* Home is a location picker, not the forecast dashboard. */
    ui.redraw = 0;
    assert(dashboard_input(&ui, "w 55") == 1);
    assert(ui.slot == 1 && ui.page == 0 && ui.redraw);
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(ui.page == 3); /* Unconfigured saved city enters city search. */
    assert(dashboard_input(&ui, "k d 218") == 0);
    assert(dashboard_input(&ui, "s Garden Grove") == 1);
    assert(!strcmp(ui.city_query, "Garden Grove"));
    assert(ui.search_request && ui.page == 2);
    assert(dashboard_input(&ui, "t u 32 150") == 1);
    assert(ui.units_f == 1);
    assert(dashboard_input(&ui, "t u 32 198") == 1);
    assert(ui.refresh_request == 1);
    assert(dashboard_input(&ui, "t u 32 246") == 1);
    assert(ui.clear_request == 1);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.page == 0);

    /* Both touch and wheel+center open a location's dashboard. */
    assert(dashboard_input(&ui, "t u 75 90") == 1);
    assert(ui.slot == 0 && ui.page == 1 && ui.day == 0);
    assert(dashboard_input(&ui, "w 55") == 1);
    assert(ui.day == 1 && ui.page == 1);
    ui.wheel_percent = 100;
    assert(dashboard_input(&ui, "w 14") == 1);
    assert(ui.day == 2);
    ui.wheel_percent = 50;
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(ui.page == 4 && ui.day == 2);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.page == 1);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.page == 0 && !ui.quit);

    assert(dashboard_input(&ui, "w 55") == 1);
    assert(ui.slot == 1);
    assert(dashboard_input(&ui, "t u 74 189") == 1);
    assert(ui.page == 3); /* Still not configured. */
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.page == 2);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.page == 0);

    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.quit);
    puts("Location-first Weather navigation, day forecast and city/settings tests passed");
    return 0;
}
