/* SPDX-License-Identifier: MIT */
#include "../src/q2pod_scheme.h"

#include <assert.h>
#include <stdio.h>

/* Values ported from DiamondBond/q2-pod: patch/offsets.inc ACCENTS,
 * assets/themes/classic-light/theme.xml, tools/theme.py GLOSS/derive. */
int main(void) {
    Q2PodScheme s;
    /* Classic Blue is its own scheme: Cobalt's tones, the verbatim band. */
    q2pod_scheme_resolve(1, 9, 1, &s);
    assert(s.painter == Q2POD_SEL_STOPS);
    assert(s.accent_top == 0x2a6ad8u && s.accent_light == 0x447cddu);
    assert(s.paper == 0xffffffu && s.ink == 0x000000u && s.muted == 0x6b6d6bu);
    assert(s.sel_text == 0xffffffu);
    assert(s.sel[0].rgb == 0x08a6e7u && s.sel[2].rgb == 0x008adeu && s.sel[5].rgb == 0x0065d6u);
    assert(s.sel_top == 0x94c7deu && s.sel_edge == 0x004184u);
    assert(s.sel_tall_edge == 0x004aa3u && s.sel_tall_line == 0x688db3u &&
           s.sel_tall_top == 0x27ade5u);
    assert(s.bar[0].rgb == 0xffffffu && s.bar[4].rgb == 0x9c9e9cu);
    assert(s.bar_line == 0x5a595au && s.bar_line2 == 0x848584u);
    assert(s.text_x == 20 && !s.caps_rows && !s.caps_bar);
    q2pod_scheme_resolve(1, 9, 0, &s);
    assert(s.paper == 0x000000u && s.ink == 0xffffffu && s.muted == 0x949294u);
    assert(s.sel_top == 0x08a6e7u && s.sel_tall_top == 0x08a6e7u);
    assert(s.bar[0].rgb == 0x1c1c1cu && s.bar_line == 0x333333u && !s.bar_line2);

    /* Classic Cobalt derives the gloss: the 38 % stop becomes Cobalt's top. */
    q2pod_scheme_resolve(1, 4, 1, &s);
    assert(s.sel[2].rgb == 0x2a6ad8u && s.sel[0].pos == 0 && s.sel[5].pos == 100);
    assert(s.accent_red == 0x447cddu);
    /* Classic Graphite derives a grey band (zero saturation stays grey). */
    q2pod_scheme_resolve(1, 0, 1, &s);
    assert(s.sel[2].rgb == 0x424242u && s.sel_top != 0 && s.sel_edge != 0);

    /* Minimal: card selection, caps rows/bar, LIGHT_PAPER/LIGHT_INK halves. */
    q2pod_scheme_resolve(0, 0, 1, &s);
    assert(s.painter == Q2POD_SEL_CARD && s.caps_rows && s.caps_bar);
    assert(s.paper == 0xf4f4f4u && s.ink == 0x161616u && s.muted == 0x8c8c8cu);
    assert(s.fill == 0xeeeeecu && s.chevron == 0x777777u && s.sel_text == 0);
    assert(s.accent_top == 0x424242u && s.text_x == 39);
    q2pod_scheme_resolve(0, 7, 0, &s); /* Ivory reuses Graphite's row */
    assert(s.accent_top == 0x424242u && s.paper == 0x000000u && s.fill == 0x2b2b2bu);
    q2pod_scheme_resolve(0, 8, 1, &s); /* Green */
    assert(s.accent_top == 0x647f34u && s.accent_light == 0x778e4cu);

    /* Modern: the accent's bar, flat LIGHT_BAR, no caps. */
    q2pod_scheme_resolve(2, 4, 1, &s);
    assert(s.painter == Q2POD_SEL_BAR && !s.caps_rows && !s.caps_bar);
    assert(s.paper == 0xf4f4f4u && s.ink == 0x161616u);
    assert(s.bar[0].rgb == 0xdededeu && s.sel_text == 0xffffffu && s.text_x == 33);
    q2pod_scheme_resolve(2, 1, 0, &s); /* Crimson, dark half */
    assert(s.accent_top == 0xe8123fu && s.accent_red == 0xff1448u);
    assert(s.paper == 0x000000u && s.bar[0].rgb == 0x000000u);

    /* Out-of-range inputs clamp instead of reading past the tables. */
    q2pod_scheme_resolve(5, 42, 1, &s);
    assert(s.painter == Q2POD_SEL_CARD && s.accent_top == 0x424242u);
    puts("q2-pod scheme values, painters and gloss derivation passed");
    return 0;
}
