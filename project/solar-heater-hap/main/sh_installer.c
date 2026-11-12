/* Installer data and the computed limits — see sh_installer.h. */

#include <math.h>
#include "sh_installer.h"

const sh_inst_info_t sh_inst_info[SH_INST_COUNT] = {
    [SH_INST_FLUID]    = { "fluid",     0, SH_FLUID_COUNT - 1 },
    [SH_INST_GLYCOL]   = { "glycol",    0,  60 },
    [SH_INST_PRESSURE] = { "press",     5,  60 },
    [SH_INST_COL_MAX]  = { "col_max", 100, 250 },
};

/* Antoine equation for water, 99 - 374 °C: log10(P / mmHg) = A - B / (C + T / °C) */
#define ANTOINE_A       8.14019
#define ANTOINE_B       1810.94
#define ANTOINE_C       244.485
#define MMHG_PER_BAR    750.062
#define ATMOSPHERE_BAR  1.013       /* added to the gauge pressure */

#define FROST_MARGIN    40          /* 4 K above the freezing point */
#define FROST_MIN       (-400)      /* range of the frost threshold */
#define FROST_MAX       100
#define COL_MAX_MARGIN  50          /* protection at most 5 K below the collector maximum ... */
#define BOILING_MARGIN  100         /* ... and 10 K below the boiling point */

typedef struct {
    int16_t pct;                /* propylene glycol, % by volume */
    int16_t val;                /* tenths of K / °C */
} point_t;

/* Boiling point rise at 1 atm (for checking against the glycol data sheet) */
static const point_t BOIL_RISE[] = { { 0, 0 }, { 20, 10 }, { 30, 20 }, { 40, 30 }, { 50, 40 }, { 60, 60 } };
/* Freezing point of the mixture (likewise) */
static const point_t FREEZING[] = { { 0, 0 }, { 10, -30 }, { 20, -80 }, { 30, -140 }, { 40, -220 },
                                    { 50, -330 }, { 60, -480 } };

/* Linear between the points of the table; outside it the first / last value. */
static double interpolate(const point_t *tab, int n, int pct)
{
    if (pct <= tab[0].pct) {
        return tab[0].val;
    }
    for (int i = 1; i < n; i++) {
        if (pct <= tab[i].pct) {
            return tab[i - 1].val + (double)(tab[i].val - tab[i - 1].val) * (pct - tab[i - 1].pct) /
                                    (tab[i].pct - tab[i - 1].pct);
        }
    }
    return tab[n - 1].val;
}

static bool value_ok(sh_inst_t id, int16_t v)
{
    return v >= sh_inst_info[id].min && v <= sh_inst_info[id].max;
}

bool sh_installer_ok(const sh_installer_t *d)
{
    for (int id = 0; id < SH_INST_COUNT; id++) {
        if (!value_ok((sh_inst_t)id, d->v[id])) {
            return false;
        }
    }
    return d->v[SH_INST_FLUID] != SH_FLUID_WATER || d->v[SH_INST_GLYCOL] == 0;
}

unsigned sh_installer_sanitize(sh_installer_t *d, const sh_installer_t *defaults)
{
    unsigned replaced = 0;
    for (int id = 0; id < SH_INST_COUNT; id++) {
        if (!value_ok((sh_inst_t)id, d->v[id])) {
            d->v[id] = defaults->v[id];
            replaced |= 1u << id;
        }
    }
    if (d->v[SH_INST_FLUID] == SH_FLUID_WATER && d->v[SH_INST_GLYCOL] != 0) {
        d->v[SH_INST_GLYCOL] = 0;
        replaced |= 1u << SH_INST_GLYCOL;
    }
    return replaced;
}

void sh_limits_calc(const sh_installer_t *d, sh_limits_t *out)
{
    const bool glycol = d->v[SH_INST_FLUID] == SH_FLUID_GLYCOL;
    const int pct = glycol ? d->v[SH_INST_GLYCOL] : 0;

    /* boiling point, tenths of °C, rounded down (the cautious side); the small term keeps a value
     * that is a whole tenth from falling to the tenth below by the floating point error */
    const double mmhg = (d->v[SH_INST_PRESSURE] / 10.0 + ATMOSPHERE_BAR) * MMHG_PER_BAR;
    const double water = ANTOINE_B / (ANTOINE_A - log10(mmhg)) - ANTOINE_C;
    out->boiling = (int16_t)floor(water * 10.0 + interpolate(BOIL_RISE, 6, pct) + 1e-6);

    const int16_t by_col = (int16_t)(d->v[SH_INST_COL_MAX] * 10 - COL_MAX_MARGIN);
    const int16_t by_boiling = (int16_t)(out->boiling - BOILING_MARGIN);
    out->protect_cap = by_col < by_boiling ? by_col : by_boiling;

    /* the freezing table is in whole tenths at every whole percent */
    int frost = (int)interpolate(FREEZING, 7, pct) + FROST_MARGIN;
    if (frost < FROST_MIN) {
        frost = FROST_MIN;
    }
    if (frost > FROST_MAX) {
        frost = FROST_MAX;
    }
    out->frost = (int16_t)frost;
}
