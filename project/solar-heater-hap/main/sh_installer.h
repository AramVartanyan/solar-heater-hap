/* Installer data (the "Service" page of the web interface) and the limits computed from them
 * (doc/operating-modes.md 2.1: "Точка на кипене на топлоносителя", "Изчислените граници",
 * "Защита от замръзване (т. 9)"; doc/tasks/step6-installer-web.md 3.1 - 3.3) — plain C, compiled
 * into the host test too. Storage in NVS: sh_installer_store.
 *
 *   coolant          water / propylene glycol
 *   glycol           0 - 60 % by volume, step 1 (always 0 with water)
 *   minimum pressure 0.5 - 6.0 bar on the gauge, cold system, step 0.1 bar (stored in 0.1 bar)
 *   collector max    100 - 250 °C, step 1 (what the collector withstands, manufacturer data)
 *
 * Computed (tenths of °C):
 *   boiling point    water at the absolute pressure (gauge + 1.013 bar), Antoine equation
 *                    (mmHg, °C: A 8.14019, B 1810.94, C 244.485), plus the glycol addition
 *                    0 / 20 / 30 / 40 / 50 / 60 % -> 0 / 1 / 2 / 3 / 4 / 6 K, linear in between;
 *                    rounded down to 0.1 °C. It is also the start ban (sh_pump_logic).
 *   protection cap   min(collector max - 5 K, boiling point - 10 K): the collector protection
 *                    setting is limited to it.
 *   frost threshold  water: 4 °C; glycol: freezing point of the mixture + 4 K, freezing points
 *                    0 / 10 / 20 / 30 / 40 / 50 / 60 % -> 0 / -3 / -8 / -14 / -22 / -33 / -48 °C,
 *                    linear in between; held in -40 ... +10 °C. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SH_COOLANT_WATER = 0,
    SH_COOLANT_GLYCOL = 1,      /* propylene glycol */
} sh_coolant_t;

typedef enum {
    SH_INST_COOLANT = 0,        /* sh_coolant_t */
    SH_INST_GLYCOL,             /* %, by volume */
    SH_INST_P_MIN,              /* 0.1 bar, gauge */
    SH_INST_COL_MAX,            /* °C */
    SH_INST_COUNT
} sh_inst_id_t;

typedef struct {
    int16_t v[SH_INST_COUNT];
} sh_installer_t;

typedef struct {
    const char *key;            /* NVS key, also the name in the web interface */
    int16_t min, max, step;
} sh_inst_info_t;

extern const sh_inst_info_t sh_inst_info[SH_INST_COUNT];

/* Computed limits, tenths of °C */
typedef struct {
    int16_t boiling;            /* boiling point = start ban */
    int16_t protect_cap;        /* collector protection at most */
    int16_t frost;              /* frost protection threshold */
} sh_limits_t;

#define SH_INST_P_ATM_MBAR      1013        /* gauge -> absolute */
#define SH_INST_FROST_WATER     40          /* 4 °C */
#define SH_INST_FROST_MARGIN    40          /* glycol: freezing point + 4 K */
#define SH_INST_FROST_MIN       (-400)
#define SH_INST_FROST_MAX       100
#define SH_INST_CAP_COL_MAX     50          /* collector max - 5 K */
#define SH_INST_CAP_BOILING     100         /* boiling point - 10 K */

/* `v` is in the table range of `id` and on its step (the coolant / glycol pair is not checked). */
bool sh_installer_value_ok(sh_inst_id_t id, int16_t v);

/* Values read from storage: each one out of range -> its default; glycol with water -> 0.
 * Returns a bit mask of the replaced values (bit = sh_inst_id_t). */
unsigned sh_installer_sanitize(sh_installer_t *s, const sh_installer_t *defaults);

/* A change of one value, checked: false (and `s` unchanged) when out of range or a glycol share
 * with water. Water as the coolant sets the glycol share to 0. */
bool sh_installer_apply(sh_installer_t *s, sh_inst_id_t id, int16_t v);

/* The computations, tenths of °C. */
int16_t sh_boiling_point(sh_coolant_t coolant, int16_t glycol_pct, int16_t p_min_dbar);
int16_t sh_protect_cap(int16_t col_max_c, int16_t boiling);
int16_t sh_frost_threshold(sh_coolant_t coolant, int16_t glycol_pct);
void sh_installer_limits(const sh_installer_t *s, sh_limits_t *out);
