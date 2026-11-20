/* SPDX-License-Identifier: MIT */
#include "../src/dashboard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Dashboard ui;
    dashboard_init(&ui);
    assert(ui.page == 0 && ui.slot == 0 && ui.home_selected == 0 && ui.redraw);

    /* Main menu wheel highlights Current, Saved and Settings. */
    Dashboard menu;
    dashboard_init(&menu);
    assert(dashboard_input(&menu, "w -56") == 1);
    assert(menu.home_selected == 2);
    assert(dashboard_input(&menu, "k d 218") == 1);
    assert(menu.page == 2 && menu.keyboard_index == 0);
    assert(dashboard_input(&menu, "k d 170") == 1);
    assert(menu.page == 0 && menu.home_selected == 2);
    assert(dashboard_input(&menu, "w 28") == 1);
    assert(menu.home_selected == 1);
    assert(dashboard_input(&menu, "t u 300 20") == 1);
    assert(menu.home_selected == 2 && menu.page == 2);

    /* Q2's clockwise wheel travel is negative: select the next row. */
    assert(dashboard_input(&ui, "w -55") == 1);
    assert(ui.home_selected == 1 && ui.slot == 0 && ui.page == 0);
    assert(dashboard_input(&ui, "w 55") == 1);
    assert(ui.home_selected == 0);
    assert(dashboard_input(&ui, "w -28") == 1);
    assert(ui.home_selected == 1);
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(ui.page == 3); /* Empty saved location opens QWERTY. */

    /* Wheel traverses QWERTY rows, center enters keys, delete edits. */
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(!strcmp(ui.city_query, "Q"));
    assert(dashboard_input(&ui, "w -28") == 1);
    assert(ui.keyboard_index == 1);
    assert(dashboard_input(&ui, "k d 218") == 1);
    assert(!strcmp(ui.city_query, "QW"));

    /* Touch backspace removes only one character, then SPACE appends. */
    assert(dashboard_input(&ui, "t u 105 127") == 1); /* E */
    assert(!strcmp(ui.city_query, "QWE"));
    assert(dashboard_input(&ui, "t u 245 257") == 1); /* drawn arrow */
    assert(!strcmp(ui.city_query, "QW"));
    assert(dashboard_input(&ui, "t u 125 257") == 1); /* SPACE */
    assert(!strcmp(ui.city_query, "QW "));
    assert(dashboard_input(&ui, "t u 184 257") == 1); /* CLEAR */
    assert(ui.city_length == 0 && !ui.city_query[0]);
    assert(dashboard_input(&ui, "t u 310 257") == 1); /* Empty SEARCH */
    assert(ui.page == 3 && !ui.search_request);

    /* Switching ABC / 123 exposes digits and punctuation without
     * discarding the previously typed city text. */
    assert(dashboard_input(&ui, "t u 59 257") == 1); /* 123 */
    assert(ui.keyboard_numbers == 1 && ui.page == 3);
    assert(dashboard_input(&ui, "t u 49 127") == 1); /* 1 */
    assert(dashboard_input(&ui, "t u 78 127") == 1); /* 2 */
    assert(dashboard_input(&ui, "t u 83 166") == 1); /* . */
    assert(!strcmp(ui.city_query, "12."));
    assert(dashboard_input(&ui, "t u 59 257") == 1); /* ABC */
    assert(ui.keyboard_numbers == 0);
    assert(dashboard_input(&ui, "t u 105 127") == 1); /* E */
    assert(!strcmp(ui.city_query, "12.E"));
    /* Wheel moves from SPACE to CLEAR, both supported with Center. */
    ui.keyboard_index = 27;
    ui.wheel_rem = 0;
    assert(dashboard_input(&ui, "w -28") == 1);
    assert(ui.keyboard_index == 28);
    assert(dashboard_input(&ui, "k d 218") == 1); /* CLEAR */
    assert(ui.city_length == 0 && !ui.city_query[0]);
    assert(dashboard_input(&ui, "t u 105 127") == 1); /* E */
    assert(dashboard_input(&ui, "t u 310 257") == 1); /* SEARCH */
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

    /* Home city can be entered manually when IP geolocation is unavailable. */
    Dashboard home;
    dashboard_init(&home);
    assert(dashboard_input(&home, "k d 218") == 1);
    assert(home.page == 3 && home.city_target == 0);
    assert(dashboard_input(&home, "k d 170") == 1);
    assert(home.page == 2);
    home.keyboard_index = 4;
    assert(dashboard_input(&home, "k d 218") == 1);
    assert(home.page == 3 && home.city_target == 0);
    assert(dashboard_input(&home, "t u 105 127") == 1);
    assert(dashboard_input(&home, "t u 310 257") == 1);
    assert(home.search_request && home.page == 2);
    home.page = 5;
    home.match_count = 1;
    assert(dashboard_input(&home, "k d 218") == 1);
    assert(home.save_request && home.city_target == 0);

    /* Touch selected location and move forward/backward through forecast. */
    ui.weather.locations[0].used = 1;
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
