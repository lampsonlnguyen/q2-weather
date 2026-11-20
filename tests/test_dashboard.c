/* SPDX-License-Identifier: MIT */
#include "../src/dashboard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Dashboard ui;
    dashboard_init(&ui);
    assert(ui.page == 0 && ui.slot == 0 && ui.redraw);

    /* Q2's clockwise wheel travel is negative: select the next row. */
    assert(dashboard_input(&ui, "w -55") == 1);
    assert(ui.slot == 1 && ui.page == 0);
    assert(dashboard_input(&ui, "w 55") == 1);
    assert(ui.slot == 0);
    assert(dashboard_input(&ui, "w -28") == 1);
    assert(ui.slot == 1);
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(ui.page == 3); /* Empty saved location opens QWERTY. */

    /* Wheel traverses QWERTY rows, center enters keys, delete edits. */
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(!strcmp(ui.city_query, "Q"));
    assert(dashboard_input(&ui, "w -28") == 1);
    assert(ui.keyboard_index == 1);
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(!strcmp(ui.city_query, "QW"));

    /* Distinguish single-character backspace from full-field Clear. */
    assert(dashboard_input(&ui, "t u 105 127") == 1); /* E */
    assert(!strcmp(ui.city_query, "QWE"));
    assert(dashboard_input(&ui, "t u 220 257") == 1); /* <-- */
    assert(!strcmp(ui.city_query, "QW"));
    assert(dashboard_input(&ui, "t u 72 257") == 1); /* SPACE */
    assert(!strcmp(ui.city_query, "QW "));
    assert(dashboard_input(&ui, "t u 145 257") == 1); /* CLEAR */
    assert(ui.city_length == 0 && !strcmp(ui.city_query, ""));
    /* Search on an empty input should stay on the QWERTY screen. */
    assert(dashboard_input(&ui, "t u 300 257") == 1); /* SEARCH */
    assert(ui.page == 3 && !ui.search_request);
    assert(dashboard_input(&ui, "t u 105 127") == 1); /* E */
    assert(!strcmp(ui.city_query, "E"));
    /* Click wheel also reaches the new Clear action (index 27). */
    ui.keyboard_index = 26;
    ui.wheel_rem = 0;
    assert(dashboard_input(&ui, "w -28") == 1);
    assert(ui.keyboard_index == 27);
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(ui.city_length == 0 && !ui.city_query[0]);
    assert(dashboard_input(&ui, "t u 105 127") == 1); /* E */
    assert(dashboard_input(&ui, "t u 300 257") == 1); /* SEARCH */
    assert(ui.search_request && ui.page == 2);

    /* Three matching cities: wheel moves through real geographic results. */
    ui.match_count = 3;
    snprintf(ui.matches[0].name, sizeof ui.matches[0].name, "Westminster");
    snprintf(ui.matches[0].region, sizeof ui.matches[0].region, "California, USA");
    ui.matches[0].latitude = 33.759;
    ui.matches[1].latitude = 39.836;
    ui.matches[2].latitude = 51.498;
    ui.page = 5;
    ui.search_result = 0;
    ui.match_selected = 0;
    ui.wheel_rem = 0;
    assert(dashboard_input(&ui, "w -28") == 1);
    assert(ui.match_selected == 1);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.page == 3 && !ui.save_request);
    ui.page = 5;
    assert(dashboard_input(&ui, "t u 95 228") == 1);
    assert(ui.match_selected == 2 && ui.save_request && ui.page == 2);

    assert(dashboard_input(&ui, "t u 42 150") == 1);
    assert(ui.units_f == 1);
    assert(dashboard_input(&ui, "t u 42 198") == 1);
    assert(ui.refresh_request == 1);
    assert(dashboard_input(&ui, "t u 42 246") == 1);
    assert(ui.clear_request == 1);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.page == 0);

    /* Touch selected location and move forward/backward through forecast. */
    assert(dashboard_input(&ui, "t u 75 90") == 1);
    assert(ui.slot == 0 && ui.page == 1 && ui.day == 0);
    assert(dashboard_input(&ui, "w -55") == 1);
    assert(ui.day == 1);
    assert(dashboard_input(&ui, "w 28") == 1);
    assert(ui.day == 0);
    ui.wheel_percent = 100;
    ui.wheel_rem = 0;
    assert(dashboard_input(&ui, "w -14") == 1);
    assert(ui.day == 1);
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(ui.page == 4 && ui.day == 1);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.page == 1);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.page == 0 && !ui.quit);
    assert(dashboard_input(&ui, "k d 170") == 1);
    assert(ui.quit);
    puts("Q2 clockwise wheel, QWERTY input, touch and forecast navigation passed");
    return 0;
}
