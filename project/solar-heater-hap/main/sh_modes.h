/* The modes above the normal control (group C): the sunrise / sunset (sh_sun), the season
 * (sh_season), the night cooling and the vacation (sh_cool) and their learned values, put
 * together once a second — plain C, no ESP-IDF / FreeRTOS, compiled into the host test and the
 * web page stand-in too. sh_control calls it before the pump decision and keeps the learned
 * values and the season in NVS when they change.
 *
 * A day for the season and the learning: the local date with a set clock (at the midnight),
 * otherwise 24 h. With a set clock the sunrise and sunset of the day before go into the lists of
 * the last 7 days (their averages are used with a reliable clock). */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_settings.h"
#include "sh_installer.h"
#include "sh_sun.h"
#include "sh_season.h"
#include "sh_cool.h"

#define SH_MODES_DAY_S          (24u * 3600u)

/* Kept in NVS: the learned values. */
typedef struct {
    sh_sun_list_t rise, set;        /* local minutes, newest first */
    sh_cool_learn_t cool;
} sh_learned_t;

void sh_learned_init(sh_learned_t *l);  /* nothing learned */

typedef struct {
    int16_t t_col, t_tank;          /* SH_TEMP_INVALID: faulty / none */
    bool pump_on;
    bool charging;                  /* the pump runs by the normal charging (difference) */
    bool tank_max;                  /* the Tmax limit is active */
    bool e4;
    const sh_settings_t *set;
    const sh_installer_t *inst;
    bool clock_set;                 /* the local time is known */
    bool reliable;                  /* ... and reliable */
    uint16_t day, min;              /* local date and minute */
    bool has_dst, dst;              /* the zone has daylight saving time; it is in effect */
} sh_modes_in_t;

#define SH_MODES_CH_SEASON      0x01    /* `season.winter` changed */
#define SH_MODES_CH_LEARNED     0x02    /* `learned` changed */

typedef struct {
    sh_sun_t sun;
    sh_season_t season;
    sh_cool_t cool;
    sh_learned_t learned;
    uint32_t period_s;              /* start of the 24 h day without a clock */
    bool period_clock;              /* the day follows the local date */
    /* the last step */
    bool winter;                    /* the season in use */
    int16_t target;                 /* of a) / b), with the learned correction */
    uint8_t sun_evt;                /* SH_SUN_EVT_* */
    uint8_t changed;                /* SH_MODES_CH_* */
} sh_modes_t;

/* `winter`: the stored season of Aut; `learned`: the stored learned values (NULL: none). */
void sh_modes_init(sh_modes_t *m, bool winter, const sh_learned_t *learned, uint32_t now_s);

/* One step; afterwards `m->cool.why` != SH_COOL_NONE is a cooling session, the vacation is
 * `in->set->v[SH_SET_VACATION]`. */
void sh_modes_step(sh_modes_t *m, const sh_modes_in_t *in, uint32_t now_s);

/* The season in use for the settings (SSn, or the one recognised in Aut). */
bool sh_modes_winter(const sh_settings_t *set, bool auto_winter);

/* Reset of the learned values (the web interface): the sunrise / sunset and the cooling target. */
void sh_modes_reset_learned(sh_modes_t *m);
