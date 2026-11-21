/* SPDX-License-Identifier: MIT */
#include "q2pod_scheme.h"

#include <math.h>
#include <string.h>

/* q2-pod's accent tones, verbatim from DiamondBond/q2-pod patch/offsets.inc
 * ACCENTS: { selection top, bottom, light tone, red tone, highlight },
 * 0xRRGGBB, in Accent-setting order. Ivory (7) reuses Graphite's row;
 * MINIMAL_FILL fills its card. */
static const uint32_t accents[9][5] = {
    { 0x424242u, 0x424242u, 0x6e6e6eu, 0xd8d8d8u, 0x555555u }, /* Graphite */
    { 0xe8123fu, 0xe8123fu, 0xeb2f56u, 0xff1448u, 0xeb2f56u }, /* Crimson */
    { 0x13838du, 0x13838du, 0x30929bu, 0x30929bu, 0x30929bu }, /* Tidal */
    { 0x8c732cu, 0x8c732cu, 0x9a8446u, 0x9a8446u, 0x9a8446u }, /* Champagne */
    { 0x2a6ad8u, 0x2a6ad8u, 0x447cddu, 0x447cddu, 0x447cddu }, /* Cobalt */
    { 0xc45400u, 0xc45400u, 0xcb681eu, 0xcb681eu, 0xcb681eu }, /* Tangerine */
    { 0x903868u, 0x903868u, 0x9d4f7au, 0x9d4f7au, 0x9d4f7au }, /* Plum */
    { 0x424242u, 0x424242u, 0x6e6e6eu, 0xd8d8d8u, 0x555555u }, /* Ivory: Graphite */
    { 0x647f34u, 0x647f34u, 0x778e4cu, 0x778e4cu, 0x778e4cu }, /* Green */
};

/* Classic Blue's selection band, verbatim from
 * assets/themes/classic-light/theme.xml <colorScheme name="classic-blue">,
 * and tools/theme.py GLOSS. The 38 % stop is Cobalt's top tone. */
static const Q2PodStop gloss_stops[Q2POD_GLOSS_STOPS] = {
    { 0, 0x08a6e7u },  { 17, 0x009adeu }, { 38, 0x008adeu },
    { 58, 0x007ddeu }, { 79, 0x006dd6u }, { 100, 0x0065d6u },
};
#define GLOSS_TOP 0x94c7deu
#define GLOSS_EDGE 0x004184u
#define GLOSS_TALL_EDGE 0x004aa3u
#define GLOSS_TALL_LINE 0x688db3u
#define GLOSS_TALL_TOP 0x27ade5u

/* Classic's gloss status bar, verbatim from classic-light/theme.xml
 * <colorScheme> light.bar.stops / dark.bar.stops. */
static const Q2PodStop classic_bar_light[Q2POD_BAR_STOPS] = {
    { 0, 0xffffffu }, { 26, 0xe7e7e7u }, { 53, 0xcecbceu }, { 79, 0xadaaadu }, { 100, 0x9c9e9cu },
};
static const Q2PodStop classic_bar_dark[Q2POD_BAR_STOPS] = {
    { 0, 0x1c1c1cu }, { 26, 0x1c1c1cu }, { 53, 0x1c1c1cu }, { 79, 0x1c1c1cu }, { 100, 0x1c1c1cu },
};

/* tools/theme.py turn(): c, a colour of Classic Blue's band, in the accent
 * whose top tone is top: its HSV hue turned by top's less the band's 38 %
 * stop's, saturation and value scaled by theirs, so that stop becomes top. */
static void rgb_to_hsv(uint32_t c, double *h, double *s, double *v) {
    double r = ((c >> 16) & 255) / 255.0, g = ((c >> 8) & 255) / 255.0, b = (c & 255) / 255.0;
    double max = fmax(r, fmax(g, b)), min = fmin(r, fmin(g, b));
    *v = max;
    double d = max - min;
    *s = max == 0 ? 0 : d / max;
    if (d == 0) {
        *h = 0;
    } else if (max == r) {
        *h = fmod((g - b) / d, 6.0) / 6.0;
        if (*h < 0) *h += 1;
    } else if (max == g) {
        *h = ((b - r) / d + 2.0) / 6.0;
    } else {
        *h = ((r - g) / d + 4.0) / 6.0;
    }
}

static uint32_t hsv_to_rgb(double h, double s, double v) {
    double r, g, b;
    if (s == 0) {
        r = g = b = v;
    } else {
        double hh = fmod(h, 1.0) * 6.0;
        if (hh < 0) hh += 6.0;
        int i = (int)hh;
        double f = hh - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch (i) {
        case 0:
            r = v;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t;
            g = p;
            b = v;
            break;
        default:
            r = v;
            g = p;
            b = q;
            break;
        }
    }
    unsigned ri = (unsigned)(r * 255 + 0.5), gi = (unsigned)(g * 255 + 0.5),
             bi = (unsigned)(b * 255 + 0.5);
    if (ri > 255) ri = 255;
    if (gi > 255) gi = 255;
    if (bi > 255) bi = 255;
    return (ri << 16) | (gi << 8) | bi;
}

static uint32_t turn_gloss(uint32_t c, uint32_t top) {
    double h0, s0, v0, h, s, v, ch, cs, cv;
    rgb_to_hsv(gloss_stops[2].rgb, &h0, &s0, &v0);
    rgb_to_hsv(top, &h, &s, &v);
    rgb_to_hsv(c, &ch, &cs, &cv);
    double ns = s0 == 0 ? cs : cs * s / s0, nv = v0 == 0 ? cv : cv * v / v0;
    if (ns > 1) ns = 1;
    if (nv > 1) nv = 1;
    return hsv_to_rgb(fmod(ch + h - h0 + 1.0, 1.0), ns, nv);
}

/* tools/theme.py resolve(): the gloss in each offered ACCENTS row that states
 * none — Classic's other accents get Classic Blue's band turned to their own
 * top tone. The light rims are the light half's; on black the dark half's
 * band starts on its first stop, and a tall row ends on its dark edge. */
static void derive_gloss(const uint32_t *tones, int light, Q2PodScheme *out) {
    uint32_t top = tones[0];
    for (int i = 0; i < Q2POD_GLOSS_STOPS; i++)
        out->sel[i] = (Q2PodStop){ gloss_stops[i].pos, turn_gloss(gloss_stops[i].rgb, top) };
    if (light) {
        out->sel_top = turn_gloss(GLOSS_TOP, top);
        out->sel_tall_top = turn_gloss(GLOSS_TALL_TOP, top);
        out->sel_tall_line = turn_gloss(GLOSS_TALL_LINE, top);
    } else {
        out->sel_top = out->sel[0].rgb;
        out->sel_tall_top = out->sel[0].rgb;
        out->sel_tall_line = turn_gloss(GLOSS_TALL_EDGE, top);
    }
    out->sel_edge = turn_gloss(GLOSS_EDGE, top);
    out->sel_tall_edge = turn_gloss(GLOSS_TALL_EDGE, top);
}

static void classic_blue(int light, Q2PodScheme *out) {
    memcpy(out->sel, gloss_stops, sizeof gloss_stops);
    if (light) {
        out->sel_top = GLOSS_TOP;
        out->sel_tall_top = GLOSS_TALL_TOP;
        out->sel_tall_line = GLOSS_TALL_LINE;
    } else {
        out->sel_top = gloss_stops[0].rgb;
        out->sel_tall_top = gloss_stops[0].rgb;
        out->sel_tall_line = GLOSS_TALL_EDGE;
    }
    out->sel_edge = GLOSS_EDGE;
    out->sel_tall_edge = GLOSS_TALL_EDGE;
}

void q2pod_scheme_resolve(int style, int accent, int light, Q2PodScheme *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (style < 0 || style > 2) style = 0;
    if (accent < 0 || accent > 9) accent = 0;
    light = !!light;
    const uint32_t *tones = accents[accent == 9 ? 4 : accent]; /* Classic Blue bases on Cobalt */
    out->accent_top = tones[0];
    out->accent_light = tones[2];
    out->accent_red = tones[3];
    out->accent_highlight = tones[4];
    out->caps_rows = style == 0;
    out->caps_bar = style == 0;
    if (style == 1) {
        /* Classic (classic-light/theme.xml): light paper white / black ink,
         * dark paper black / white ink; second line #949294, flipped to
         * #6b6d6b on the light half; sel.text white in both halves. */
        out->painter = Q2POD_SEL_STOPS;
        out->text_x = 20; /* geometry text_x */
        out->paper = light ? 0xffffffu : 0x000000u;
        out->ink = light ? 0x000000u : 0xffffffu;
        out->muted = light ? 0x6b6d6bu : 0x949294u;
        out->fill = out->paper;
        out->chevron = 0xffffffu;
        out->sel_text = 0xffffffu;
        if (accent == 9)
            classic_blue(light, out);
        else
            derive_gloss(tones, light, out);
        memcpy(out->bar, light ? classic_bar_light : classic_bar_dark, sizeof out->bar);
        out->bar_line = light ? 0x5a595au : 0x333333u;
        out->bar_line2 = light ? 0x848584u : 0;
    } else if (style == 2) {
        /* Modern (modern/theme.xml extends minimal; ROLE_MACROS): the
         * accent's bar over Minimal's layout; flat bar LIGHT_BAR in the
         * light half, iPod black status chrome (BAR_COLOR) in the dark.
         * muted has no Modern override (ROLE_MACROS lists 0), so it
         * inherits Minimal's SUDO_MUTED. White text holds 4.5:1 on every
         * accent top (offsets.inc ACCENTS comment). */
        out->painter = Q2POD_SEL_BAR;
        out->text_x = 33;                           /* HOME_MODERN_TEXT_X */
        out->paper = light ? 0xf4f4f4u : 0x000000u; /* LIGHT_PAPER / dark half */
        out->ink = light ? 0x161616u : 0xffffffu;   /* LIGHT_INK / dark half */
        out->muted = 0x8c8c8cu;                     /* SUDO_MUTED */
        out->fill = light ? 0xf2f2f7u : 0x1c1c1cu;  /* flat home pane stops */
        out->chevron = 0xffffffu;
        out->sel_text = 0xffffffu;
        for (int i = 0; i < Q2POD_BAR_STOPS; i++)
            out->bar[i] = (Q2PodStop){ (unsigned char)(i * 25), light ? 0xdededeu : 0x000000u };
        out->bar_line = light ? 0xdededeu : 0x000000u;
    } else {
        /* Minimal (minimal/theme.xml; ROLE_MACROS): card selection
         * (SEL_INSET 6, SEL_RADIUS 6), caps rows, mono icons; the card
         * fills with MINIMAL_FILL on paper, the stock dark surface
         * (FILL_RGB) on black; the accent dot is the accent's top tone. */
        out->painter = Q2POD_SEL_CARD;
        out->text_x = 39;                           /* HOME_DOT_X + HOME_DOT + HOME_DOT_GAP */
        out->paper = light ? 0xf4f4f4u : 0x000000u; /* LIGHT_PAPER / dark half */
        out->ink = light ? 0x161616u : 0xffffffu;   /* LIGHT_INK / dark half */
        out->muted = 0x8c8c8cu;                     /* SUDO_MUTED */
        out->fill = light ? 0xeeeeecu : 0x2b2b2bu;  /* MINIMAL_FILL / FILL_RGB */
        out->chevron = 0x777777u;                   /* MINIMAL_CHEVRON */
        out->sel_text = 0;                          /* the row's own ink on the card */
        for (int i = 0; i < Q2POD_BAR_STOPS; i++)
            out->bar[i] = (Q2PodStop){ (unsigned char)(i * 25), light ? 0xeeeeecu : 0x000000u };
        out->bar_line = light ? 0xeeeeecu : 0x000000u;
    }
}
