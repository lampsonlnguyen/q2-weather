/* SPDX-License-Identifier: MIT */
#ifndef Q2_WEATHER_Q2POD_SCHEME_H
#define Q2_WEATHER_Q2POD_SCHEME_H
#include <stdint.h>

/* q2-pod's own selection painters (DiamondBond/q2-pod patch/theme.h):
 * Minimal's card, Classic's stops, Modern's bar. */
enum { Q2POD_SEL_CARD, Q2POD_SEL_STOPS, Q2POD_SEL_BAR };

#define Q2POD_GLOSS_STOPS 6
#define Q2POD_BAR_STOPS 5

typedef struct {
    /* A gradient stop: position 0-100 (% down the band) and 0xRRGGBB colour.
     * q2-pod patch/theme.h stops_t. */
    unsigned char pos;
    uint32_t rgb;
} Q2PodStop;

typedef struct {
    int painter;   /* Q2POD_SEL_* */
    int caps_rows; /* Minimal's rows and bar title are small capitals */
    int caps_bar;
    int text_x;                 /* List text's left edge (classic-light geometry text_x,
                                   HOME_MODERN_TEXT_X, Minimal HOME_DOT_X + HOME_DOT + HOME_DOT_GAP) */
    uint32_t paper, ink, muted; /* This half's surfaces (scheme_t paper/ink/muted) */
    uint32_t fill;              /* Minimal MINIMAL_FILL card / Modern flat pane ground */
    uint32_t chevron;           /* Minimal MINIMAL_CHEVRON; Classic/Modern use sel_text */
    uint32_t sel_text;          /* Text on the selection; 0 means the row's own ink */
    /* The accent's ACCENTS row (patch/offsets.inc): top, bottom, light, red, highlight. */
    uint32_t accent_top, accent_light, accent_red, accent_highlight;
    Q2PodStop sel[Q2POD_GLOSS_STOPS]; /* Classic's gloss band (sel.stops) */
    uint32_t sel_top;  /* Its first pixel row (light half) / first stop (dark half) */
    uint32_t sel_edge; /* Its last pixel row */
    uint32_t sel_tall_edge, sel_tall_line, sel_tall_top; /* A tall row's last two rows */
    Q2PodStop bar[Q2POD_BAR_STOPS]; /* Classic's gloss status bar (bar.stops) */
    uint32_t bar_line, bar_line2;   /* The bar's last row; light half's row above it */
} Q2PodScheme;

/* Geometry q2-pod's Classic set uses for every list (classic-light/theme.xml
 * <geometry name="classic-light">): 26 px bar, 32 px rows. Minimal and Modern
 * share the same 32 px rows here so wheel/touch geometry stays theme-independent;
 * Minimal's HOME_TOP 8 px inset is not applied. */
#define Q2POD_BAR_H 26
#define Q2POD_ROW_H 32

/* Resolve the scheme for a parsed theme: style 0 Minimal, 1 Classic, 2 Modern;
 * accent 0-8 the ACCENTS row, 9 Classic Blue; light 0 dark half, 1 light half.
 * Out-of-range inputs clamp to Graphite / Minimal / dark. */
void q2pod_scheme_resolve(int style, int accent, int light, Q2PodScheme *out);
#endif
