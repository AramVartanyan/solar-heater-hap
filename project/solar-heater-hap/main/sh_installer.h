/* Installer data and the limits computed from them (doc/operating-modes.md 2.1: boiling point of
 * the heat transfer fluid, computed limits, frost protection p. 9;
 * doc/tasks/step6-installer-web.md 3.1 - 3.3) — plain C, no ESP-IDF / FreeRTOS, compiled into the
 * host test too. Storage in NVS: sh_installer_store.
 *
 *   boiling point   water at the absolute pressure (gauge + 1.013 bar, Antoine equation) plus the
 *                   glycol rise (0 / 20 / 30 / 40 / 50 / 60 % -> 0 / 1 / 2 / 3 / 4 / 6 K, linear
 *                   in between), rounded down to 0.1 °C
 *   protection cap  min(collector maximum - 5 K, boiling point - 10 K): the ceiling of the
 *                   collector protection setting
 *   no start        = the boiling point (the pump logic releases it below it - 2 K)
 *   frost threshold water 4 °C; propylene glycol: the freezing point of the mixture
 *                   (0 / 10 / ... / 60 % -> 0 / -3 / -8 / -14 / -22 / -33 / -48 °C, linear in
 *                   between) + 4 K; held within -40 ... +10 °C
 *   vacation        the collector protection during the vacation (doc 2.1, p. 8): 75 % of the
 *                   collector maximum, at most the boiling point - 10 K
 * Group C (doc/tasks/step10-cooling-vacation-seasons.md 3.1, 3.2, 3.5): the collector type and
 * area and the tank volume give the target of the night cooling (sh_cool_target); the heat
 * transfer fluid gives the pause of the air venting (sh_air_pause_s). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SH_FLUID_WATER = 0,
    SH_FLUID_GLYCOL,            /* propylene glycol with water */
    SH_FLUID_COUNT
} sh_fluid_t;

typedef enum {
    SH_INST_FLUID = 0,          /* sh_fluid_t */
    SH_INST_GLYCOL,             /* propylene glycol, 0 - 60 % by volume; 0 with water */
    SH_INST_PRESSURE,           /* minimum pressure on the gauge (cold system), 5 - 60 = 0.5 - 6.0 bar */
    SH_INST_COL_MAX,            /* maximum collector temperature, 100 - 250 °C */
    SH_INST_COL_TYPE,           /* sh_col_type_t */
    SH_INST_AREA,               /* collector area, 5 - 200 = 0.5 - 20.0 m² */
    SH_INST_TANK_VOL,           /* tank volume, 50 - 1000 l, step 10 */
    SH_INST_COUNT
} sh_inst_t;

typedef enum {
    SH_COL_FLAT = 0,            /* flat plate */
    SH_COL_TUBES,               /* evacuated tubes */
    SH_COL_TYPE_COUNT
} sh_col_type_t;

typedef struct {
    int16_t v[SH_INST_COUNT];
} sh_installer_t;

typedef struct {
    const char *key;            /* NVS key, also the name in the web interface */
    int16_t min, max, step;
} sh_inst_info_t;

extern const sh_inst_info_t sh_inst_info[SH_INST_COUNT];

/* Every value is in its range, on its step, and the glycol is 0 with water. */
bool sh_installer_ok(const sh_installer_t *d);

/* Values read from storage: each one out of range (or off its step) -> its default; glycol with
 * water -> 0.
 * Returns a bit mask of the replaced values (bit = sh_inst_t). */
unsigned sh_installer_sanitize(sh_installer_t *d, const sh_installer_t *defaults);

/* Tenths of °C. */
typedef struct {
    int16_t boiling;            /* boiling point = no start of the pump at or above it */
    int16_t protect_cap;        /* ceiling of the collector protection */
    int16_t frost;              /* frost protection: on below it */
    int16_t vac_protect;        /* collector protection during the vacation */
} sh_limits_t;

void sh_limits_calc(const sh_installer_t *d, sh_limits_t *out);

/* Night cooling target (doc 2.1, p. 7 — group C), 0.1 °C: the energy of a clear summer day
 * (flat 2.5 kWh/m², tubes 3.0 kWh/m²) raises the tank by dT = energy / (volume x 1.163 Wh/(l K));
 * the target is 95 °C - dT (rounded down), plus the learned correction `learned` (0.1 K), held
 * within Tmax - 20 K ... Tmax (`tank_max`, 0.1 °C). */
#define SH_COOL_KWH_FLAT        25      /* 0.1 kWh per m² */
#define SH_COOL_KWH_TUBES       30
#define SH_COOL_SPAN            200     /* the target is at least Tmax - 20 K */
int16_t sh_cool_target(const sh_installer_t *d, int16_t tank_max, int16_t learned);

/* Air venting (doc 2.1, "Обезвъздушаване" — group C): 3 cycles of 5 min run + a pause of
 * 10 min x the viscosity factor of the fluid (water 1; propylene glycol 10 % 1.3, 30 % 2.5,
 * 40 % 3.5, 50 % 4.5, 60 % 5.5, linear in between). */
#define SH_AIR_CYCLES           3
#define SH_AIR_RUN_S            300u
#define SH_AIR_PAUSE_BASE_S     600u
uint32_t sh_air_pause_s(const sh_installer_t *d);
