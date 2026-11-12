/* Installer data and the computed limits — see sh_installer.h. */

#include <math.h>
#include "sh_installer.h"

const sh_inst_info_t sh_inst_info[SH_INST_COUNT] = {
    [SH_INST_COOLANT] = { "coolant",   0,   1, 1 },
    [SH_INST_GLYCOL]  = { "glycol",    0,  60, 1 },
    [SH_INST_P_MIN]   = { "p_min",     5,  60, 1 },
    [SH_INST_COL_MAX] = { "col_max", 100, 250, 1 },
};

/* Antoine equation of water: log10(p / mmHg) = A - B / (C + t / °C) */
#define ANTOINE_A       8.14019
#define ANTOINE_B       1810.94
#define ANTOINE_C       244.485
#define MMHG_PER_BAR    750.062

/* A table of points 0, step, 2 * step ... %, value linear in between (tenths). */
typedef struct {
    int16_t pct;
    int16_t v;
} point_t;

/* Boiling point rise of the glycol mixture at 1 atm, tenths of K */
static const point_t s_glycol_boil[] = {
    { 0, 0 }, { 20, 10 }, { 30, 20 }, { 40, 30 }, { 50, 40 }, { 60, 60 },
};

/* Freezing point of the mixture, tenths of °C */
static const point_t s_glycol_freeze[] = {
    { 0, 0 }, { 10, -30 }, { 20, -80 }, { 30, -140 }, { 40, -220 }, { 50, -330 }, { 60, -480 },
};

#define N_POINTS(t)     (sizeof(t) / sizeof((t)[0]))

/* Linear interpolation, in tenths as a real number; held at the end points. */
static double interpolate(const point_t *t, unsigned n, int16_t pct)
{
    if (pct <= t[0].pct) {
        return t[0].v;
    }
    for (unsigned i = 1; i < n; i++) {
        if (pct <= t[i].pct) {
            return t[i - 1].v +
                   (double)(t[i].v - t[i - 1].v) * (pct - t[i - 1].pct) / (t[i].pct - t[i - 1].pct);
        }
    }
    return t[n - 1].v;
}

bool sh_installer_value_ok(sh_inst_id_t id, int16_t v)
{
    if ((unsigned)id >= SH_INST_COUNT) {
        return false;
    }
    const sh_inst_info_t *i = &sh_inst_info[id];
    return v >= i->min && v <= i->max && (v - i->min) % i->step == 0;
}

unsigned sh_installer_sanitize(sh_installer_t *s, const sh_installer_t *defaults)
{
    unsigned replaced = 0;
    for (int id = 0; id < SH_INST_COUNT; id++) {
        if (!sh_installer_value_ok((sh_inst_id_t)id, s->v[id])) {
            s->v[id] = defaults->v[id];
            replaced |= 1u << id;
        }
    }
    if (s->v[SH_INST_COOLANT] == SH_COOLANT_WATER && s->v[SH_INST_GLYCOL] != 0) {
        s->v[SH_INST_GLYCOL] = 0;
        replaced |= 1u << SH_INST_GLYCOL;
    }
    return replaced;
}

bool sh_installer_apply(sh_installer_t *s, sh_inst_id_t id, int16_t v)
{
    if (!sh_installer_value_ok(id, v)) {
        return false;
    }
    if (id == SH_INST_GLYCOL && v != 0 && s->v[SH_INST_COOLANT] == SH_COOLANT_WATER) {
        return false;
    }
    s->v[id] = v;
    if (id == SH_INST_COOLANT && v == SH_COOLANT_WATER) {
        s->v[SH_INST_GLYCOL] = 0;
    }
    return true;
}

int16_t sh_boiling_point(sh_coolant_t coolant, int16_t glycol_pct, int16_t p_min_dbar)
{
    const double p_abs_bar = p_min_dbar / 10.0 + SH_INST_P_ATM_MBAR / 1000.0;
    const double water = ANTOINE_B / (ANTOINE_A - log10(p_abs_bar * MMHG_PER_BAR)) - ANTOINE_C;
    double add = 0;             /* K */
    if (coolant == SH_COOLANT_GLYCOL) {
        add = interpolate(s_glycol_boil, N_POINTS(s_glycol_boil), glycol_pct) / 10.0;
    }
    /* rounded down to 0.1 °C: the safer side */
    return (int16_t)floor((water + add) * 10.0);
}

int16_t sh_protect_cap(int16_t col_max_c, int16_t boiling)
{
    const int cap_col = col_max_c * 10 - SH_INST_CAP_COL_MAX;
    const int cap_boil = boiling - SH_INST_CAP_BOILING;
    return (int16_t)(cap_col < cap_boil ? cap_col : cap_boil);
}

int16_t sh_frost_threshold(sh_coolant_t coolant, int16_t glycol_pct)
{
    if (coolant != SH_COOLANT_GLYCOL) {
        return SH_INST_FROST_WATER;
    }
    /* rounded up (the safer side); the table gives whole tenths for whole percents anyway */
    int t = (int)ceil(interpolate(s_glycol_freeze, N_POINTS(s_glycol_freeze), glycol_pct)) +
            SH_INST_FROST_MARGIN;
    if (t < SH_INST_FROST_MIN) {
        t = SH_INST_FROST_MIN;
    }
    if (t > SH_INST_FROST_MAX) {
        t = SH_INST_FROST_MAX;
    }
    return (int16_t)t;
}

void sh_installer_limits(const sh_installer_t *s, sh_limits_t *out)
{
    const sh_coolant_t coolant = (sh_coolant_t)s->v[SH_INST_COOLANT];
    out->boiling = sh_boiling_point(coolant, s->v[SH_INST_GLYCOL], s->v[SH_INST_P_MIN]);
    out->protect_cap = sh_protect_cap(s->v[SH_INST_COL_MAX], out->boiling);
    out->frost = sh_frost_threshold(coolant, s->v[SH_INST_GLYCOL]);
}
