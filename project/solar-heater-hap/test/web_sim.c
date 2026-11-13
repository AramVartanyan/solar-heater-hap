/* Stand-in for the firmware, to try the page of the web interface in a browser on the computer
 * (doc/tasks/step6-installer-web.md 3.5, step9-clock-history.md 3.4): serves main/web/index.html
 * and the API of main/sh_web.c with the same plain-C code (sh_web_api, sh_installer, sh_settings,
 * sh_pump_ctl, sh_alarm; sh_clock_rules, sh_tz, sh_day, sh_hist; sh_sun, sh_season, sh_cool —
 * doc/tasks/step10-cooling-vacation-seasons.md 3.6). Nothing is stored; the values
 * start at the Kconfig defaults. The clock is the one of the computer (set "by SNTP", zone
 * Europe/Sofia confirmed); the chart starts with a made-up day and the history with 40 made-up
 * days (in a RAM "flash").
 *
 *   make -C project/solar-heater-hap/test sim        ->  http://localhost:8080/
 *   build/web_sim ../main/web/index.html [port]
 *
 * The access code is 810840. The measurement is set by hand:
 *   http://localhost:8080/sim?col=1105&tank=720      temperatures, tenths of °C
 *   http://localhost:8080/sim?cf=1&tf=0              sensor faults (collector, tank)
 *   http://localhost:8080/sim?net=0                  answers stop for a while ("no connection")
 *   http://localhost:8080/sim?clock=0                the clock is lost (power cut): not set, no
 *                                                    SNTP -> the page sets it; clock=1: SNTP again
 *   http://localhost:8080/sim?tzok=0                 the zone is not confirmed
 *   http://localhost:8080/sim?peak=0                 the peak check says "not reliable"
 *   http://localhost:8080/sim?hist=0                 no history partition (grey)
 *   http://localhost:8080/sim?hhmm=0330              the local time becomes 03:30 (the clock is moved)
 *   http://localhost:8080/sim?e4=1                   an E4 "during the day": cooling from 03:00
 *   http://localhost:8080/sim?heater=1               the heater works against the cooling (information)
 *   http://localhost:8080/sim?hdet=1                 the heater detected (shown with "Heater 0 kW")
 *   http://localhost:8080/sim?leak=1                 a leak check of the heater starts now (5 min);
 *                                                    leak=2: the leak is stopped; leak=0: idle
 *   http://localhost:8080/sim?e8=1                   E8 (3 nights with the reverse thermosiphon sign)
 * The pulse mode (group D2, doc/tasks/step13-pulse-mode.md 3.6): "Service" -> "Sensor mounting" ->
 * "On the pipes". /sim?col=...&tank=... are then the pipes: the page shows the values taken 30 s
 * after a pulse started ("measured at the pulse at ..."), the pump waits for the next pulse ("in ...
 * min"); a pulse with the collector at the boiling point - 10 K stops the pulses (the warning).
 * The control runs at every request of the page (every 5 s), so a pulse lasts 30 - 35 s here.
 * The heater (group D, doc/tasks/step12-heater-leak-e8.md 3.5): the sign follows the tank with the
 * pump stopped (/sim?tank=... rising by the threshold in 10 min), the leak checks run as in the
 * firmware (outside 09:00 - 16:00 / the learned daylight), E8 is judged at the end of the night.
 * Night cooling in the browser: /sim?col=150&tank=720 and /sim?hhmm=0400 (tank above Tmax), or
 * /sim?e4=1 and /sim?hhmm=0300; the vacation at night: switch it on and /sim?hhmm=2300 (the learned
 * sunset is 16:50); the season follows the daylight saving time of the zone (Aut) or the choice.
 * The network and the firmware update (group E, doc/tasks/step7-network-ota.md 3.4) run on the
 * rules of the firmware (sh_net_rules, sh_ota) with a made-up update server:
 *   http://localhost:8080/sim?ap=1       nEt -> AP while the router does not answer: the own access
 *                                        point (the page stays on localhost; the browser counts
 *                                        as its client); ap=0: nEt -> CP
 *   http://localhost:8080/sim?apidle=1   the 15 min without a client are over: back to CP
 *   http://localhost:8080/sim?fw=0       the server has nothing newer; fw=1: version 0.2.0 (default)
 *   http://localhost:8080/sim?srv=0      no internet: the check / the update fails; srv=1: back
 *   http://localhost:8080/sim?fup=1      short "menu" while "FUp" is offered (the automatic check
 *                                        comes 1 min after the start; the console says when)
 * A check answers after 2 s, an update takes 30 s and "restarts" into the new version; with the pump
 * running (/sim?col=900&tank=400) a confirmed update waits.
 * Apple HomeKit (group F, doc/tasks/step14-homekit.md): the setting and its refusal while paired are
 * the rules of the firmware (sh_hap_rules); HAP "runs" from the start, not paired, the pairing
 * window open:
 *   http://localhost:8080/sim?hkpair=1   a controller is paired (the switch becomes inactive, with
 *                                        the note); hkpair=0: removed in Apple Home (the window opens)
 *   http://localhost:8080/sim?hkwin=0    the 10 min of the pairing window are over
 *   http://localhost:8080/sim?hkrun=0    HAP is not running (no connection to the router since
 *                                        boot): the pairings cannot be reset; hkrun=1: running
 * The switch and "Reset -> HomeKit pairings" "restart" the controller (the console says so).
 * POSIX sockets, one request at a time. */

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "sh_state.h"
#include "sh_pump_ctl.h"
#include "sh_alarm.h"
#include "sh_web_api.h"
#include "sh_clock_rules.h"
#include "sh_tz.h"
#include "sh_day.h"
#include "sh_hist.h"
#include "sh_sun.h"
#include "sh_season.h"
#include "sh_cool.h"
#include "sh_heater.h"
#include "sh_e8.h"
#include "sh_pulse.h"
#include "sh_net_rules.h"
#include "sh_ota.h"
#include "sh_hap_rules.h"

#define CODE    "810840"
#define AP_SSID "Solar-" CODE    /* the one name: the network, the host name, mDNS */

static sh_web_config_t s_cfg = {
    .settings = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } },
    .frost = true,
    .inst = { .v = { SH_FLUID_WATER, 0, 15, 120, SH_COL_FLAT, 20, 150, 20 } },
};
static sh_pump_temps_t s_temps = { .t_col = 523, .t_tank = 418 };
static sh_pump_ctl_t s_pump;
static sh_alarm_state_t s_alarm;
static sh_sun_t s_sun;
static sh_season_t s_season;
static sh_cool_t s_cool;
static sh_heater_t s_heater;
static sh_leak_t s_leak;
static sh_e8_t s_e8;
static sh_pulse_t s_pulse;
static bool s_pulse_mode;
static bool s_winter, s_dst;
static uint16_t s_sunrises, s_sunsets;
static int16_t s_cool_target;
static const sh_alarm_params_t ALARM = {
    .e6_time_s = 600, .e6_col_rise = 100, .e6_tank_rise = 5, .e6_diff = 200, .e6_clear_diff = 100,
};
static time_t s_start, s_silent_until;
/* Apple HomeKit: the setting, paired, HAP running, the pairing window */
static bool s_hap_on = true, s_hap_paired, s_hap_run = true, s_hap_win = true;

/* monotonic s since the "boot", which was a day before the start (room for the made-up day) */
static uint32_t now_s(void)
{
    return (uint32_t)(time(NULL) - s_start) + 86400u;
}

/* ---- network mode, firmware update (the server is made up) ---- */

static sh_net_t s_net;
static sh_ota_t s_ota;
static char s_version[16] = "0.1.0";
static bool s_fw_new = true, s_inet = true;
static sh_ota_act_t s_job;              /* what the "server" is busy with ... */
static uint32_t s_job_ms;               /* ... since */

static uint32_t sim_ms(void)
{
    return (uint32_t)(time(NULL) - s_start) * 1000u;
}

static void net_do(sh_net_act_t act)
{
    static const char *const names[] = { "", "AP: the own access point starts", "CP: the router",
                                         "CP: no stored network, the radio is off" };
    if (act != SH_NET_ACT_NONE) {
        printf("nEt -> %s\n", names[act]);
    }
    if (act == SH_NET_ACT_START_AP) {       /* the browser is its client */
        sh_net_event(&s_net, SH_NET_EVT_AP_STARTED, true, now_s());
        sh_net_event(&s_net, SH_NET_EVT_CLIENT_JOINED, true, now_s());
    }
}

static void ota_do(sh_ota_act_t act)
{
    if (act != SH_OTA_ACT_NONE) {
        s_job = act;
        s_job_ms = sim_ms();
        printf("firmware: %s\n", act == SH_OTA_ACT_CHECK ? "check" : "update started");
    }
}

/* The rules, as the task sh_net steps them, and the answers of the made-up server. */
static void net_step(const sh_ota_in_t *in)
{
    const uint32_t ms = sim_ms();
    const bool was_offer = s_ota.offer;
    ota_do(sh_ota_step(&s_ota, in, ms));
    if (s_job == SH_OTA_ACT_CHECK && ms - s_job_ms >= 2000) {
        s_job = SH_OTA_ACT_NONE;
        sh_ota_event(&s_ota, !s_inet ? SH_OTA_EVT_FAILED : s_fw_new ? SH_OTA_EVT_AVAILABLE : SH_OTA_EVT_CURRENT,
                     s_fw_new ? "0.2.0" : s_version, 0, ms);
    } else if (s_job == SH_OTA_ACT_UPDATE && s_ota.phase == SH_OTA_RUNNING) {
        const int pct = (int)((ms - s_job_ms) / 300);
        if (!s_inet || !s_fw_new) {
            s_job = SH_OTA_ACT_NONE;
            sh_ota_event(&s_ota, s_inet ? SH_OTA_EVT_CURRENT : SH_OTA_EVT_FAILED, s_version, 0, ms);
        } else if (pct >= 100) {
            sh_ota_event(&s_ota, SH_OTA_EVT_SUCCESS, "0.2.0", 100, ms);
        } else {
            sh_ota_event(&s_ota, SH_OTA_EVT_AVAILABLE, "0.2.0", 0, ms);
            sh_ota_event(&s_ota, SH_OTA_EVT_PROGRESS, "0.2.0", pct / 5 * 5, ms);
        }
    } else if (s_job == SH_OTA_ACT_UPDATE && s_ota.phase == SH_OTA_DONE && ms - s_job_ms >= 35000) {
        printf("firmware: restart into 0.2.0\n");      /* the restart: nothing is kept but the version */
        snprintf(s_version, sizeof(s_version), "0.2.0");
        s_fw_new = false;
        s_job = SH_OTA_ACT_NONE;
        memset(&s_ota, 0, sizeof(s_ota));
        s_ota.boot_done = true;
        memset(&s_net, 0, sizeof(s_net));
    }
    if (s_ota.offer && !was_offer) {
        printf("FUp: version %s offered for 5 s — /sim?fup=1 is the short \"menu\"\n", s_ota.version);
    }
    net_do(sh_net_step(&s_net, sh_ota_updating(&s_ota), true, now_s()));
}

/* ---- clock, day, history ---- */

static bool s_clock_set = true, s_sntp = true, s_tz_ok = true;
static uint32_t s_sntp_s;
static long long s_offset;              /* the time of the browser - the time of the computer */
static int s_tz = 1;                    /* Europe/Sofia */
static sh_peak_t s_peak = { .reliable = true, .peak = 13 * 60 + 40 };
static sh_day_t s_day;
static bool s_hist_on = true;
static uint8_t s_flash[64 * 1024];
static sh_hist_t s_hist;

static bool f_read(void *ctx, uint32_t off, void *buf, uint32_t len)
{
    (void)ctx;
    memcpy(buf, s_flash + off, len);
    return true;
}

static bool f_write(void *ctx, uint32_t off, const void *buf, uint32_t len)
{
    (void)ctx;
    for (uint32_t i = 0; i < len; i++) {
        s_flash[off + i] &= ((const uint8_t *)buf)[i];
    }
    return true;
}

static bool f_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    memset(s_flash + off, 0xFF, len);
    return true;
}

static const sh_hist_flash_t FLASH = {
    .read = f_read, .write = f_write, .erase = f_erase, .size = sizeof(s_flash), .sector = 4096,
};

static long long sim_epoch(void)
{
    return (long long)time(NULL) + s_offset;
}

static void local_at(long long epoch, sh_local_t *lt)
{
    lt->set = s_clock_set;
    lt->day = SH_DAY_NONE;
    lt->min = 0;
    if (s_clock_set) {
        setenv("TZ", sh_tz_list[s_tz].rule, 1);
        tzset();
        const time_t t = (time_t)epoch;
        struct tm tm;
        localtime_r(&t, &tm);
        lt->day = sh_day_number(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        lt->min = (uint16_t)(tm.tm_hour * 60 + tm.tm_min);
        s_dst = tm.tm_isdst > 0;
    }
}

static sh_clock_trust_t trust(void)
{
    return sh_clock_trust(s_clock_set, s_tz_ok, s_peak.reliable);
}

/* A finished day, as sh_history writes it. */
static void day_over(const sh_day_sum_t *d)
{
    sh_hist_rec_t r;
    sh_hist_rec_init(&r, d->day);
    r.col_max = d->col_max == SH_TEMP_INVALID ? SH_HIST_NO_TEMP : d->col_max;
    r.col_min = d->col_min == SH_TEMP_INVALID ? SH_HIST_NO_TEMP : d->col_min;
    r.tank_max = d->tank_max == SH_TEMP_INVALID ? SH_HIST_NO_TEMP : d->tank_max;
    r.tank_min = d->tank_min == SH_TEMP_INVALID ? SH_HIST_NO_TEMP : d->tank_min;
    for (int k = 0; k < 3; k++) {
        r.run_min[k] = (uint16_t)((d->run_s[k] + 30) / 60);
    }
    r.cool_min = (uint16_t)((d->run_s[SH_RUN_COOL] + 30) / 60);
    r.heater_min = (uint16_t)((d->heater_s + 30) / 60);
    r.sunrise = d->sunrise >= 0 ? (uint16_t)d->sunrise : SH_HIST_NO_TIME;
    r.sunset = d->sunset >= 0 ? (uint16_t)d->sunset : SH_HIST_NO_TIME;
    r.cool_target = (uint8_t)((s_cool_target + 5) / 10);
    r.alarms = d->alarms;
    r.peak_min = d->peak_min;
    r.clock_reliable = trust() == SH_CLOCK_RELIABLE;
    r.partial = !d->full;
    if (s_hist_on) {
        sh_hist_append(&s_hist, &r);
    }
    if (s_tz_ok) {
        sh_peak_day(&s_peak, d->full, !d->col_fault, d->col_min, d->col_max, d->peak_min);
    }
    printf("day %u over (%s)\n", d->day, d->full ? "full" : "partial");
}

/* A made-up sunny day by the local minute: the collector peaks at ~13:40, the pump charges, a test at
 * 08:00, the collector protection around noon, night cooling 04:00 - 04:40. */
static void made_up(int min, sh_day_in_t *in, int16_t *tank)
{
    const double x = (min - 400) / 760.0;                   /* 06:40 ... 19:20 */
    const double sun = x > 0 && x < 1 ? sin(x * 3.14159) : 0;
    in->t_col = (int16_t)(120 + 1020 * pow(sun, 1.6));
    in->t_tank = *tank;
    in->pump_on = in->t_col - *tank >= 60 || (min >= 480 && min < 485);
    in->reason = min >= 480 && min < 485 ? SH_PUMP_REASON_TEST
               : in->t_col >= 1100 ? SH_PUMP_REASON_COL_PROTECT : SH_PUMP_REASON_DIFF;
    if (in->pump_on && in->reason != SH_PUMP_REASON_TEST && *tank < 680) {
        *tank = (int16_t)(*tank + 1);
    } else if (!in->pump_on && *tank > 380 && min % 6 == 0) {
        *tank = (int16_t)(*tank - 1);
    }
    in->alarms = in->reason == SH_PUMP_REASON_COL_PROTECT ? SH_ALARM_BIT(SH_ALARM_HOT) : 0;
    if (min >= 240 && min < 280) {
        in->pump_on = true;
        in->reason = SH_PUMP_REASON_COOL;
    }
    in->sunrises = min >= 415 ? 1 : 0;
    in->sunsets = min >= 1010 ? 1 : 0;
}

static void made_up_start(void)
{
    sh_day_init(&s_day);
    const long long now = sim_epoch();
    int16_t tank = 420;
    sh_day_sum_t done;
    for (uint32_t k = 86400; k >= 60; k -= 60) {             /* the last 24 h, a step a minute */
        sh_local_t lt;
        local_at(now - k, &lt);
        sh_day_in_t in = { 0 };
        made_up(lt.min, &in, &tank);
        in.sunrises = in.sunsets = 0;                       /* (the counters of the control start at 0) */
        if (sh_day_step(&s_day, &in, &lt, now_s() - k, &done)) {
            done.full = true;                               /* (the made-up day before) */
        }
    }
    /* learned: a week of sunrises around 06:55 and sunsets around 16:50, the target 1 K down */
    sh_sun_days_reset(&s_sun.days);
    for (int i = 0; i < SH_SUN_DAYS; i++) {
        s_sun.days.rise[i] = (uint16_t)(412 + i);
        s_sun.days.set[i] = (uint16_t)(1013 - i);
    }
    s_cool.offset = -1;
    sh_e8_learned_reset(&s_e8.l);
    for (int i = 0; i < SH_E8_DAYS; i++) {
        s_e8.l.rate[i] = (int16_t)(4 + i % 3);                 /* ~0.5 K/h */
    }
    /* 40 made-up days before today; 3 days ago partial, 10 days ago E1 */
    sh_hist_open(&s_hist, &FLASH);
    sh_local_t lt;
    local_at(now, &lt);
    for (int d = 40; d >= 1; d--) {
        sh_hist_rec_t r;
        sh_hist_rec_init(&r, (uint16_t)(lt.day - d));
        r.col_max = (int16_t)(700 + (d * 37) % 450);
        r.col_min = (int16_t)(80 + (d * 13) % 60);
        r.tank_max = (int16_t)(500 + (d * 17) % 180);
        r.tank_min = (int16_t)(380 + (d * 7) % 60);
        r.run_min[SH_RUN_CHARGE] = (uint16_t)(150 + (d * 29) % 200);
        r.run_min[SH_RUN_PROTECT] = (uint16_t)(r.col_max > 1100 ? 25 : 0);
        r.run_min[SH_RUN_OTHER] = (uint16_t)(d % 7 == 0 ? 1 : 0);
        r.cool_min = (uint16_t)(d % 5 == 0 ? 35 + d : 0);
        r.heater_min = (uint16_t)(d % 3 == 0 ? 90 + d : 0);
        r.sunrise = (uint16_t)(415 - d);
        r.sunset = (uint16_t)(1010 + d);
        r.cool_target = 65;
        r.peak_min = (int16_t)(13 * 60 + (d * 11) % 120);
        r.clock_reliable = true;
        r.partial = d == 3;
        r.alarms = (uint16_t)((d == 10 ? SH_ALARM_BIT(SH_ALARM_E1) : 0) |
                              (r.run_min[SH_RUN_PROTECT] ? SH_ALARM_BIT(SH_ALARM_HOT) : 0));
        sh_hist_append(&s_hist, &r);
    }
}

/* One step of the control, as sh_control does it every second. */
static void control(void)
{
    sh_limits_t lim;
    sh_limits_calc(&s_cfg.inst, &lim);
    const uint32_t now = now_s();
    const int16_t *set = s_cfg.settings.v;
    sh_local_t lt;
    local_at(sim_epoch(), &lt);
    const bool reliable = trust() == SH_CLOCK_RELIABLE;
    const int16_t col = s_temps.col_fault ? SH_TEMP_INVALID : s_temps.t_col;
    const int16_t tank = s_temps.tank_fault ? SH_TEMP_INVALID : s_temps.t_tank;
    const bool pulse = s_cfg.inst.v[SH_INST_MOUNT] == SH_MOUNT_PIPES;
    if (pulse != s_pulse_mode) {
        s_pulse_mode = pulse;
        memset(&s_pulse, 0, sizeof(s_pulse));
        printf("sensors %s\n", pulse ? "on the pipes: pulse mode" : "in the sleeves");
    }
    const bool rel = pulse && s_pump.pulse_mode && s_pump.rel.any;
    const int16_t v_col = pulse ? (rel && !s_temps.col_fault ? s_pump.rel.col : SH_TEMP_INVALID) : col;
    const int16_t v_tank = pulse ? (rel && !s_temps.tank_fault ? s_pump.rel.tank : SH_TEMP_INVALID) : tank;

    /* sunrise / sunset, season, night cooling — in the order of sh_control */
    const uint8_t sun = sh_sun_step(&s_sun, col, tank, s_pump.on, &lt, reliable, now);
    s_sunrises = (uint16_t)(s_sunrises + ((sun & SH_SUN_EVT_RISE) ? 1 : 0));
    s_sunsets = (uint16_t)(s_sunsets + ((sun & SH_SUN_EVT_SET) ? 1 : 0));
    const bool has_dst = strchr(sh_tz_list[s_tz].rule, ',') != NULL;
    s_winter = sh_season_step(&s_season, (sh_season_mode_t)set[SH_SET_SEASON], s_winter, v_col, s_pump.on,
                              reliable && has_dst, s_dst, now);
    const bool vacation = set[SH_SET_VACATION] != 0;
    sh_pump_params_t p = {
        .diff_on = (int16_t)(set[sh_settings_seasonal(SH_SET_DIFF_ON, s_winter)] * 10),
        .diff_off = (int16_t)(set[sh_settings_seasonal(SH_SET_DIFF_OFF, s_winter)] * 10),
        .min_run_s = pulse ? SH_PULSE_RUN_S : 180, .min_off_s = 120,
        .tank_max = (int16_t)(set[SH_SET_TANK_MAX] * 10),
        .col_min = (int16_t)(set[sh_settings_seasonal(SH_SET_COL_MIN, s_winter)] * 10),
        .col_protect = vacation ? lim.vacation : 1100, .col_protect_cap = lim.protect_cap,
        .col_boiling = lim.boiling, .frost_on = s_cfg.frost, .frost = lim.frost, .charge_off = vacation,
        .heater_rise = sh_heater_rise(s_cfg.inst.v[SH_INST_HEATER], s_cfg.inst.v[SH_INST_TANK_VOL]),
        .pulse = pulse,
    };
    s_cool_target = sh_cool_target(s_cfg.inst.v[SH_INST_COL_TYPE] == SH_COL_TUBES, s_cfg.inst.v[SH_INST_COL_AREA],
                                   s_cfg.inst.v[SH_INST_TANK_VOL], p.tank_max, s_cool.offset);
    const sh_cool_in_t cin = {
        .reliable = reliable, .min = lt.min, .t_col = v_col, .t_tank = v_tank, .tank_max = p.tank_max,
        .target = s_cool_target, .tmax_on = set[sh_settings_seasonal(SH_SET_NCOOL, s_winter)] != 0,
        .vacation = vacation, .e4 = (s_alarm.active & SH_ALARM_BIT(SH_ALARM_E4)) != 0,
        .night = sh_sun_night(&s_sun, &lt, reliable), .dark = s_sun.dark, .sunset_s = s_sun.sunset_s,
        .pump_cool = s_pump.on && s_pump.reason == SH_PUMP_REASON_COOL,
    };
    sh_cool_step(&s_cool, &cin, now);
    if (s_cool.events & SH_COOL_EVT_END) {
        printf("night cooling ended (%d)\n", s_cool.end);
    }
    if (s_cool.events & SH_COOL_EVT_START) {
        printf("night cooling started (%d), target %d\n", s_cool.trigger, s_cool_target);
    }
    p.cool = s_cool.trigger != SH_COOL_NONE;

    /* the heater, its leak, E8 — in the order of sh_control */
    const int16_t power = s_cfg.inst.v[SH_INST_HEATER];
    const bool heater_on = sh_pump_ctl_heater(&s_pump);
    if (sh_heater_step(&s_heater, power, heater_on)) {
        printf("heater detected with 0 kW\n");
    }
    const sh_leak_in_t lin = {
        .heater = !pulse && sh_heater_present(&s_heater, power), .reliable = reliable, .min = lt.min,
        .rise = sh_sun_avg(s_sun.days.rise), .set = sh_sun_avg(s_sun.days.set), .t_col = col, .pump_on = s_pump.on,
        .charging = s_pump.on && s_pump.mode == SH_PUMP_MODE_NORMAL &&
                    (s_pump.reason == SH_PUMP_REASON_DIFF || s_pump.reason == SH_PUMP_REASON_MIN_RUN),
        .sunrise = (sun & SH_SUN_EVT_RISE) != 0,
    };
    sh_leak_step(&s_leak, &lin, now);
    if (s_leak.events) {
        printf("heater leak: events 0x%02x, phase %d, last drop %d\n", s_leak.events, s_leak.phase, s_leak.last_drop);
    }
    p.leak_check = s_leak.phase == SH_LEAK_CHECK;
    p.leak_stop = s_leak.phase == SH_LEAK_STOP;
    p.leak_resume = s_leak.resume;
    const sh_e8_in_t ein = {
        .night = sh_sun_night(&s_sun, &lt, reliable), .t_col = col, .t_tank = tank, .pump_on = s_pump.on,
        .heater = heater_on,
        .threshold = sh_e8_threshold(s_cfg.inst.v[SH_INST_COL_TYPE] == SH_COL_TUBES, s_cfg.inst.v[SH_INST_COL_AREA],
                                     s_cfg.inst.v[SH_INST_TANK_VOL]),
    };
    sh_e8_step(&s_e8, &ein, now);
    if (s_e8.events) {
        printf("E8: events 0x%02x, %u sign / %u normal nights, active %d\n", s_e8.events, s_e8.l.sign_nights,
               s_e8.l.normal_nights, s_e8.l.active);
    }
    if (pulse) {
        const sh_pulse_in_t pin = {
            .reliable = reliable, .lt = lt, .rise = sh_sun_avg(s_sun.days.rise), .set = sh_sun_avg(s_sun.days.set),
            .risen = s_sun.risen, .pump_on = s_pump.on, .pipe_col = col,
            .rel = rel, .rel_col = v_col, .rel_s = s_pump.rel.at_s, .fresh = rel && s_pump.rel.fresh,
            .cool = s_cool.pulse, .frost_on = p.frost_on, .frost = p.frost, .boiling = p.col_boiling,
        };
        sh_pulse_step(&s_pulse, &pin, now);
        p.pulse_due = s_pulse.due;
    }
    sh_pump_ctl_step(&s_pump, &p, &s_temps, now);
    if (s_pump.events & SH_PUMP_EVT_PULSE_END) {
        printf("pulse: collector %d, tank %d\n", s_pump.rel.col, s_pump.rel.tank);
    }
    s_pump.events = 0;
    const sh_alarm_in_t in = {
        .t_col = s_temps.t_col, .t_tank = s_temps.t_tank,
        .col_fault = s_temps.col_fault, .tank_fault = s_temps.tank_fault,
        .pump_on = s_pump.on, .reason = s_pump.reason,
        .col_boiling = s_pump.norm.col_boiling, .tank_ceiling = s_pump.norm.tank_ceiling, .e8 = s_e8.l.active,
    };
    sh_alarm_step(&s_alarm, &ALARM, &in, now);

    /* the network mode and the firmware update, as sh_net does it */
    const sh_ota_in_t oin = { .online = s_net.mode == SH_NET_CP, .pump_on = s_pump.on, .alarms = s_alarm.active,
                              .air = s_pump.air };
    net_step(&oin);

    /* the day, as sh_history does it every second */
    const sh_day_in_t din = {
        .t_col = pulse ? s_pump.rel.col : s_temps.t_col, .t_tank = pulse ? s_pump.rel.tank : s_temps.t_tank, .col_fault = s_temps.col_fault,
        .tank_fault = s_temps.tank_fault, .pump_on = s_pump.on, .reason = s_pump.reason,
        .alarms = s_alarm.active, .sunrises = s_sunrises, .sunsets = s_sunsets, .heater = heater_on,
    };
    sh_day_sum_t done;
    if (sh_day_step(&s_day, &din, &lt, now, &done)) {
        day_over(&done);
    }
}

static void reply_ex(int fd, const char *status, const char *type, const char *extra, const char *body,
                     size_t len)
{
    char head[384];
    const int n = snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                           "%sCache-Control: no-store\r\nConnection: close\r\n\r\n", status, type, len, extra);
    if (write(fd, head, (size_t)n) < 0 || write(fd, body, len) < 0) {
        perror("write");
    }
}

static void reply(int fd, const char *status, const char *type, const char *body, size_t len)
{
    reply_ex(fd, status, type, "", body, len);
}

static void reply_json(int fd, const char *json, int len)
{
    reply(fd, len < 0 ? "500 Internal Server Error" : "200 OK", "application/json", json, len < 0 ? 0 : (size_t)len);
}

static void reply_res(int fd, sh_web_res_t res)
{
    static const char *const status[] = { "200 OK", "400 Bad Request", "403 Forbidden" };
    static const char *const body[] = { "{\"ok\":true}", "{\"ok\":false,\"err\":\"value\"}",
                                        "{\"ok\":false,\"err\":\"code\"}" };
    reply(fd, status[res], "application/json", body[res], strlen(body[res]));
}

static void reply_state(int fd)
{
    control();
    const uint32_t now = now_s();
    sh_local_t lt;
    local_at(sim_epoch(), &lt);
    const bool pulse = s_pulse_mode;
    sh_web_status_t s = {
        .t_col = pulse ? s_pump.rel.col : s_temps.t_col, .t_tank = pulse ? s_pump.rel.tank : s_temps.t_tank,
        .col_fault = s_temps.col_fault, .tank_fault = s_temps.tank_fault,
        .pump_on = s_pump.on, .reason = s_pump.reason, .alarms = s_alarm.active,
        .test_on = s_pump.test, .test_left_s = sh_pump_ctl_test_left(&s_pump, now),
        .cfg = s_cfg, .col_protect = 1100,
        .name = AP_SSID, .version = s_version, .serial = "E8F60A810840",
        .ssid = s_net.mode == SH_NET_AP ? "" : "A16blue", .ip = s_net.mode == SH_NET_AP ? "192.168.4.1" : "192.168.1.57",
        .rssi = s_net.mode == SH_NET_AP ? 0 : -41,
        .ap = s_net.mode == SH_NET_AP, .ap_pending = s_net.ap_pending, .host = AP_SSID, .ap_ssid = AP_SSID,
        .ota = s_ota.phase, .ota_pct = s_ota.pct, .ota_version = s_ota.version,
        .hap_on = s_hap_on, .hap_paired = s_hap_paired, .hap_running = s_hap_on && s_hap_run,
        .hap_window = s_hap_on && s_hap_run && s_hap_win,
        .clock = { .trust = trust(), .sntp = s_sntp, .min = lt.set ? (int16_t)lt.min : -1,
                   .tz = sh_tz_list[s_tz].name, .tz_confirmed = s_tz_ok, .peak = s_peak.peak },
        .history = s_hist_on,
        .winter = s_winter,
        .cool = (uint8_t)s_cool.trigger, .cool_target = s_cool_target,
        .cool_until = trust() == SH_CLOCK_RELIABLE && s_cool.trigger != SH_COOL_VACATION ? SH_COOL_TO_MIN : -1,
        .cool_heater = s_cool.heater, .cool_learned = s_cool_target,
        .sunrise = (int16_t)sh_sun_avg(s_sun.days.rise), .sunset = (int16_t)sh_sun_avg(s_sun.days.set),
        .heater_on = sh_pump_ctl_heater(&s_pump), .heater_det = s_heater.detected, .leak = (uint8_t)s_leak.phase,
        .leak_left_s = sh_leak_left(&s_leak, now), .leak_last = s_leak.last, .leak_last_stop = s_leak.last_stop,
        .leak_last_drop = s_leak.last_drop, .e8_normal = sh_e8_normal(&s_e8.l),
        .pulse = pulse, .meas = pulse && s_pump.rel.any, .meas_age_s = now - s_pump.rel.at_s,
        .meas_pulse = s_pump.rel.by_pulse, .meas_now = pulse && s_pump.rel.fresh,
        .pulse_next = pulse && !s_pump.on && s_pulse.next_s != SH_PULSE_NONE,
        .pulse_left_s = s_pulse.next_s > now ? s_pulse.next_s - now : 0, .pulse_hot = pulse && s_pulse.hot,
    };
    sh_air_view_t air;
    sh_pump_ctl_air(&s_pump, now, &air);
    s.air_on = air.on;
    s.air_run = air.run;
    s.air_cycle = air.cycle;
    s.air_left_s = air.left_s;
    sh_day_run_today(&s_day, &lt, now, s.run_s);
    static char json[SH_WEB_STATUS_MAX];
    const int len = sh_web_status_json(json, sizeof(json), &s);
    reply(fd, len < 0 ? "500 Internal Server Error" : "200 OK", "application/json", json,
          len < 0 ? 0 : (size_t)len);
}

/* /sim?key=value&... */
static void sim_set(const char *query)
{
    int v;
    const char *p;
    if ((p = strstr(query, "col=")) && sscanf(p + 4, "%d", &v) == 1) {
        s_temps.t_col = (int16_t)v;
    }
    if ((p = strstr(query, "tank=")) && sscanf(p + 5, "%d", &v) == 1) {
        s_temps.t_tank = (int16_t)v;
    }
    if ((p = strstr(query, "cf=")) && sscanf(p + 3, "%d", &v) == 1) {
        s_temps.col_fault = v != 0;
    }
    if ((p = strstr(query, "tf=")) && sscanf(p + 3, "%d", &v) == 1) {
        s_temps.tank_fault = v != 0;
    }
    if ((p = strstr(query, "net=")) && sscanf(p + 4, "%d", &v) == 1) {
        s_silent_until = v ? 0 : time(NULL) + 20;
    }
    if ((p = strstr(query, "clock=")) && sscanf(p + 6, "%d", &v) == 1) {
        s_clock_set = s_sntp = v != 0;                  /* 0: lost (power cut), 1: set by SNTP */
        s_sntp_s = now_s();
        s_offset = 0;
    }
    if ((p = strstr(query, "tzok=")) && sscanf(p + 5, "%d", &v) == 1) {
        s_tz_ok = v != 0;
    }
    if ((p = strstr(query, "peak=")) && sscanf(p + 5, "%d", &v) == 1) {
        s_peak.reliable = v != 0;
    }
    if ((p = strstr(query, "hist=")) && sscanf(p + 5, "%d", &v) == 1) {
        s_hist_on = v != 0;
    }
    if ((p = strstr(query, "hhmm=")) && sscanf(p + 5, "%d", &v) == 1) {
        sh_local_t lt;
        local_at(sim_epoch(), &lt);                     /* the clock is moved to this local time */
        s_offset += ((v / 100) * 60 + v % 100 - (int)lt.min) * 60;
    }
    if ((p = strstr(query, "e4=")) && sscanf(p + 3, "%d", &v) == 1) {
        s_cool.e4_pending = v != 0;
    }
    if ((p = strstr(query, "heater=")) && sscanf(p + 7, "%d", &v) == 1) {
        s_cool.heater = v != 0;
    }
    if ((p = strstr(query, "hdet=")) && sscanf(p + 5, "%d", &v) == 1) {
        s_heater.detected = v != 0;
    }
    if ((p = strstr(query, "leak=")) && sscanf(p + 5, "%d", &v) == 1) {
        s_leak.phase = v == 1 ? SH_LEAK_CHECK : v == 2 ? SH_LEAK_STOP : SH_LEAK_IDLE;
        s_leak.check_s = now_s();
        s_leak.check_ref = s_temps.t_col;
        s_leak.warm_n = 0;
        if (v == 2) {
            s_leak.last = true;
            s_leak.last_stop = true;
            s_leak.last_drop = 31;
        }
    }
    if ((p = strstr(query, "e8=")) && sscanf(p + 3, "%d", &v) == 1) {
        s_e8.l.active = v != 0;
    }
    if ((p = strstr(query, "fw=")) && sscanf(p + 3, "%d", &v) == 1) {
        s_fw_new = v != 0;
    }
    if ((p = strstr(query, "srv=")) && sscanf(p + 4, "%d", &v) == 1) {
        s_inet = v != 0;
    }
    if ((p = strstr(query, "fup=")) && sscanf(p + 4, "%d", &v) == 1 && v) {
        const sh_ota_in_t in = { .online = s_net.mode == SH_NET_CP, .pump_on = s_pump.on, .alarms = s_alarm.active,
                                 .air = s_pump.air };
        ota_do(sh_ota_cmd(&s_ota, SH_OTA_CMD_CONFIRM, &in, sim_ms()));
        printf("FUp: \"menu\" -> %s\n", s_ota.phase == SH_OTA_RUNNING ? "the update starts"
               : s_ota.phase == SH_OTA_WAITING ? "the update waits for the pump" : "nothing is offered");
    }
    if ((p = strstr(query, "hkpair=")) && sscanf(p + 7, "%d", &v) == 1) {
        s_hap_paired = v != 0;
        s_hap_win = sh_hap_window(s_hap_win, v ? SH_HAP_EVT_PAIRED : SH_HAP_EVT_UNPAIRED, v ? 1 : 0);
    }
    if ((p = strstr(query, "hkwin=")) && sscanf(p + 6, "%d", &v) == 1 && !v) {
        s_hap_win = sh_hap_window(s_hap_win, SH_HAP_EVT_TIMED_OUT, s_hap_paired);
    }
    if ((p = strstr(query, "hkrun=")) && sscanf(p + 6, "%d", &v) == 1) {
        s_hap_run = v != 0;
        s_hap_win = v && sh_hap_window(false, SH_HAP_EVT_STARTED, s_hap_paired);
    }
    if ((p = strstr(query, "ap=")) && sscanf(p + 3, "%d", &v) == 1) {
        /* the choice of the menu; AP is possible because "the router does not answer" */
        net_do(sh_net_choose(&s_net, v ? SH_NET_AP : SH_NET_CP, false, sh_ota_updating(&s_ota), now_s()));
    }
    if ((p = strstr(query, "apidle=")) && sscanf(p + 7, "%d", &v) == 1 && v && s_net.mode == SH_NET_AP) {
        s_net.clients = 0;                              /* the last client left 15 min ago */
        s_net.idle_s = now_s() - SH_NET_AP_IDLE_S;
    }
}

typedef struct {
    sh_hist_rec_t rec[SH_WEB_DAYS];
    unsigned n;
} days_t;

static bool day_take(void *ctx, const sh_hist_rec_t *r)
{
    days_t *d = ctx;
    d->rec[d->n++] = *r;
    return true;
}

typedef struct {
    char *buf;
    size_t size, len;
} csv_t;

static bool csv_line(void *ctx, const sh_hist_rec_t *r)
{
    csv_t *c = ctx;
    const int n = sh_hist_csv_line(c->buf + c->len, c->size - c->len, r);
    if (n > 0) {
        c->len += (size_t)n;
    }
    return n > 0;
}

static void handle(int fd, const char *page, size_t page_len)
{
    static char req[4096];
    size_t got = 0;
    char *body = NULL;
    size_t need = 0;
    /* the head, then as much body as Content-Length says */
    while (got < sizeof(req) - 1) {
        const ssize_t r = read(fd, req + got, sizeof(req) - 1 - got);
        if (r <= 0) {
            return;
        }
        got += (size_t)r;
        req[got] = '\0';
        if (!body && (body = strstr(req, "\r\n\r\n")) != NULL) {
            body += 4;
            const char *cl = strstr(req, "Content-Length:");
            if (!cl) {
                cl = strstr(req, "content-length:");
            }
            need = cl ? (size_t)atoi(cl + 15) : 0;
        }
        if (body && got - (size_t)(body - req) >= need) {
            break;
        }
    }
    if (!body) {
        return;
    }
    char method[8], path[128];
    if (sscanf(req, "%7s %127s", method, path) != 2) {
        return;
    }
    const bool post = strcmp(method, "POST") == 0;
    printf("%s %s %s\n", method, path, post ? body : "");

    if (strncmp(path, "/sim", 4) == 0) {
        sim_set(path);
        reply(fd, "200 OK", "text/plain", "ok\n", 3);
    } else if (time(NULL) < s_silent_until) {
        return;                                 /* the connection is just closed */
    } else if (!post && strcmp(path, "/") == 0) {
        reply(fd, "200 OK", "text/html; charset=utf-8", page, page_len);
    } else if (!post && strcmp(path, "/api/state") == 0) {
        reply_state(fd);
    } else if (!post && strcmp(path, "/api/chart") == 0) {
        control();
        sh_local_t lt;
        local_at(sim_epoch(), &lt);
        static char json[SH_WEB_CHART_MAX];
        reply_json(fd, json, sh_web_chart_json(json, sizeof(json), &s_day, &lt, now_s()));
    } else if (!post && strcmp(path, "/api/days") == 0) {
        days_t d = { .n = 0 };
        if (s_hist_on) {
            sh_hist_walk(&s_hist, true, SH_WEB_DAYS, day_take, &d);
        }
        static char json[SH_WEB_DAYS_MAX];
        reply_json(fd, json, sh_web_days_json(json, sizeof(json), s_hist_on, d.rec, d.n));
    } else if (!post && strcmp(path, "/api/history.csv") == 0) {
        static char csv[2048 * SH_HIST_CSV_LINE_MAX];
        csv_t c = { .buf = csv, .size = sizeof(csv) };
        c.len = (size_t)snprintf(csv, sizeof(csv), "%s", sh_hist_csv_header);
        if (!s_hist_on) {
            reply(fd, "404 Not Found", "text/plain", "no history\n", 11);
        } else {
            sh_hist_walk(&s_hist, false, 0, csv_line, &c);
            reply_ex(fd, "200 OK", "text/csv; charset=utf-8",
                     "Content-Disposition: attachment; filename=\"" AP_SSID "-history.csv\"\r\n", csv, c.len);
        }
    } else if (!post && strcmp(path, "/api/tz") == 0) {
        static char json[SH_WEB_TZ_MAX];
        reply_json(fd, json, sh_web_tz_json(json, sizeof(json)));
    } else if (post && strcmp(path, "/api/time") == 0) {
        int64_t t;
        const sh_web_res_t res = sh_web_time(body, &t);
        if (res != SH_WEB_OK) {
            reply_res(fd, res);
        } else {
            const bool taken = sh_clock_browser_ok(t, s_sntp, s_sntp_s, now_s());
            if (taken) {
                s_offset = (long long)t - (long long)time(NULL);
                s_clock_set = true;
            }
            const char *ans = taken ? "{\"ok\":true,\"taken\":1}" : "{\"ok\":true,\"taken\":0}";
            reply(fd, "200 OK", "application/json", ans, strlen(ans));
        }
    } else if (post && strcmp(path, "/api/tz") == 0) {
        int tz;
        const sh_web_res_t res = sh_web_tz(body, CODE, &tz);
        if (res == SH_WEB_OK) {
            s_tz = tz;
            s_tz_ok = true;
            sh_peak_reset(&s_peak, true);
        }
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/set") == 0) {
        unsigned changed;
        reply_res(fd, sh_web_set(body, CODE, &s_cfg, &changed));
    } else if (post && strcmp(path, "/api/test") == 0) {
        sh_web_test_t cmd = SH_WEB_TEST_NONE;
        const sh_web_res_t res = sh_web_test(body, s_pump.test, &cmd);
        if (res == SH_WEB_OK && cmd == SH_WEB_TEST_PLUS) {
            sh_pump_ctl_plus(&s_pump, (uint32_t)s_cfg.settings.v[SH_SET_TEST_S], now_s());
        } else if (res == SH_WEB_OK && cmd == SH_WEB_TEST_MINUS) {
            sh_pump_ctl_minus(&s_pump, now_s());
        } else if (res == SH_WEB_OK && cmd == SH_WEB_TEST_STOP) {
            sh_pump_ctl_stop(&s_pump, now_s());
        }
        control();
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/air") == 0) {
        sh_web_air_t cmd = SH_WEB_AIR_NONE;
        const sh_web_res_t res = sh_web_air(body, s_pump.air, s_pump.test, &cmd);
        if (res == SH_WEB_OK && cmd == SH_WEB_AIR_START) {
            sh_pump_ctl_air_start(&s_pump, sh_air_pause_s(&s_cfg.inst), now_s());
        } else if (res == SH_WEB_OK && cmd == SH_WEB_AIR_STOP) {
            sh_pump_ctl_air_stop(&s_pump);
        }
        control();
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/reset") == 0) {
        sh_web_reset_t what;
        const sh_web_res_t res = sh_web_reset(body, CODE, &what);
        if (res == SH_WEB_OK && what == SH_WEB_RESET_HISTORY) {
            sh_hist_clear(&s_hist);
        } else if (res == SH_WEB_OK && what == SH_WEB_RESET_LEARNED) {
            sh_peak_reset(&s_peak, s_tz_ok);
            sh_sun_days_reset(&s_sun.days);
            s_cool.offset = 0;
            s_cool.quiet = 0;
            s_heater.detected = false;
            for (int i = 0; i < SH_E8_DAYS; i++) {
                s_e8.l.rate[i] = SH_E8_NO_RATE;
            }
        } else if (what == SH_WEB_RESET_HOMEKIT) {
            if (res == SH_WEB_OK && !(s_hap_on && s_hap_run)) {
                reply_res(fd, SH_WEB_BAD);              /* HAP is not running */
                return;
            }
            if (res == SH_WEB_OK) {                     /* erased; after the "restart" the window is open */
                s_hap_paired = false;
                s_hap_win = sh_hap_window(false, SH_HAP_EVT_STARTED, 0);
                printf("HomeKit pairings erased: the controller restarts\n");
            }
        } else if (res == SH_WEB_OK && what != SH_WEB_RESET_WIFI) {
            s_cfg = (sh_web_config_t){ .settings = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } }, .frost = true,
                                       .inst = { .v = { SH_FLUID_WATER, 0, 15, 120, SH_COL_FLAT, 20, 150, 20 } } };
        }
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/ota") == 0) {
        sh_ota_cmd_t cmd = SH_OTA_CMD_CHECK;
        const sh_web_res_t res = sh_web_ota(body, CODE, &cmd);
        if (res == SH_WEB_OK) {
            const sh_ota_in_t in = { .online = s_net.mode == SH_NET_CP, .pump_on = s_pump.on,
                                     .alarms = s_alarm.active, .air = s_pump.air };
            ota_do(sh_ota_cmd(&s_ota, cmd, &in, sim_ms()));
        }
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/hap") == 0) {
        bool on = false;
        const sh_web_res_t res = sh_web_hap(body, CODE, s_hap_on, s_hap_paired, &on);
        if (res == SH_WEB_OK && on != s_hap_on) {
            s_hap_on = on;                              /* stored; HAP starts (or not) after the "restart" */
            s_hap_win = on && s_hap_run && sh_hap_window(false, SH_HAP_EVT_STARTED, s_hap_paired);
            printf("Apple HomeKit %s: the controller restarts\n", on ? "on" : "off");
        }
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/auth") == 0) {
        reply_res(fd, sh_web_code_ok(body, CODE) ? SH_WEB_OK : SH_WEB_DENIED);
    } else {
        reply(fd, "404 Not Found", "text/plain", "not found\n", 10);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <index.html> [port]\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    static char page[256 * 1024];
    const size_t page_len = fread(page, 1, sizeof(page), f);
    fclose(f);
    const int port = argc > 2 ? atoi(argv[2]) : 8080;

    const int srv = socket(AF_INET, SOCK_STREAM, 0);
    const int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (srv < 0 || bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(srv, 8) < 0) {
        perror("listen");
        return 1;
    }
    s_start = time(NULL);
    s_sntp_s = now_s();
    made_up_start();
    printf("page %zu bytes, http://localhost:%d/ (access code %s)\n", page_len, port, CODE);
    fflush(stdout);
    for (;;) {
        const int fd = accept(srv, NULL, NULL);
        if (fd < 0) {
            continue;
        }
        handle(fd, page, page_len);
        fflush(stdout);
        close(fd);
    }
}
