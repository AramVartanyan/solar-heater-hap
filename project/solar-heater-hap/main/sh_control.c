#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "sh_state.h"
#include "sh_pump_ctl.h"
#include "sh_alarm.h"
#include "sh_ind.h"
#include "sh_settings_store.h"
#include "sh_installer_store.h"
#include "sh_learn_store.h"
#include "sh_clock.h"
#include "sh_sun.h"
#include "sh_season.h"
#include "sh_cool.h"
#include "sh_heater.h"
#include "sh_e8.h"
#include "sh_relay.h"
#include "sh_led.h"
#include "sh_control.h"

static const char *TAG = "sh_control";

#define CONTROL_PERIOD_MS   1000

_Static_assert(CONFIG_SH_DIFF_OFF_K < CONFIG_SH_DIFF_ON_K,
               "Control: the switch-off difference must be lower than the switch-on difference");
_Static_assert(CONFIG_SH_E6_CLEAR_DIFF_K < CONFIG_SH_E6_DIFF_K,
               "Control: the E6 clearing difference must be lower than the E6 difference");
_Static_assert(CONFIG_SH_LED_COLD_C + 2 < CONFIG_SH_LED_HOT_C - 2,
               "Status LED: the cold and hot collector zones must not overlap");

/* E6 (Kconfig "Control"): minutes, K -> seconds, tenths */
static const sh_alarm_params_t ALARM_PARAMS = {
    .e6_time_s = CONFIG_SH_E6_TIME_MIN * 60,
    .e6_col_rise = CONFIG_SH_E6_COL_RISE_K * 10,
    .e6_tank_rise = CONFIG_SH_E6_TANK_RISE_DK,
    .e6_diff = CONFIG_SH_E6_DIFF_K * 10,
    .e6_clear_diff = CONFIG_SH_E6_CLEAR_DIFF_K * 10,
};

/* White / yellow colour of the status LED, °C -> tenths */
static const sh_ind_params_t IND_PARAMS = {
    .cold = CONFIG_SH_LED_COLD_C * 10,
    .hot = CONFIG_SH_LED_HOT_C * 10,
};

/* Kept between the steps (only the control task uses them) */
static sh_alarm_state_t s_alarm;
static sh_ind_state_t s_ind;
static sh_ind_in_t s_ind_in;
static bool s_ind_done;

/* Season, sunrise / sunset, night cooling (only the control task uses them) */
static sh_sun_t s_sun;
static sh_season_t s_season;
static sh_cool_t s_cool;
static sh_modes_t s_modes;
static bool s_modes_done;
static bool s_vacation;

/* The heater, its leak, E8 (only the control task uses them) */
static sh_heater_t s_heater;
static sh_leak_t s_leak;
static sh_e8_t s_e8;

static QueueHandle_t s_cmds;

/* The menu settings (whole K / °C; the seasonal ones of the current season), Kconfig "Control"
 * (minutes, °C) -> tenths and seconds, and the limits computed from the installer data. Read on
 * every cycle: a change applies at once. In the vacation the collector protection works at the
 * vacation limit and there is no normal charging. */
static void get_params(sh_pump_params_t *p, const sh_settings_t *s, bool winter, const sh_limits_t *lim,
                       const sh_installer_t *inst)
{
    const bool vacation = s->v[SH_SET_VACATION] != 0;
    memset(p, 0, sizeof(*p));       /* padding too: the logged copy is compared by its fields only */
    p->diff_on = (int16_t)(s->v[sh_settings_seasonal(SH_SET_DIFF_ON, winter)] * 10);
    p->diff_off = (int16_t)(s->v[sh_settings_seasonal(SH_SET_DIFF_OFF, winter)] * 10);
    p->min_run_s = CONFIG_SH_MIN_RUN_MIN * 60;
    p->min_off_s = CONFIG_SH_MIN_OFF_MIN * 60;
    p->tank_max = (int16_t)(s->v[SH_SET_TANK_MAX] * 10);
    p->col_min = (int16_t)(s->v[sh_settings_seasonal(SH_SET_COL_MIN, winter)] * 10);
    p->col_protect = vacation ? lim->vacation : SH_CONTROL_COL_PROTECT;
    p->col_protect_cap = lim->protect_cap;
    p->col_boiling = lim->boiling;
    p->frost_on = sh_settings_frost();
    p->frost = lim->frost;
    p->charge_off = vacation;
    p->heater_rise = sh_heater_rise(inst->v[SH_INST_HEATER], inst->v[SH_INST_TANK_VOL]);
}

static void save_learned(void)
{
    const sh_learned_t l = { .days = s_sun.days, .cool_offset = s_cool.offset, .cool_quiet = s_cool.quiet,
                             .heater = s_heater.detected, .e8 = s_e8.l };
    sh_learn_save(&l);
}

/* monotonic seconds since boot (not the time of day) */
static uint32_t now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/* "23.4", "-0.5" or "---" for the log */
static const char *temp_str(char *buf, size_t size, int16_t t_tenths, bool fault)
{
    if (fault || t_tenths == SH_TEMP_INVALID) {
        return "---";
    }
    snprintf(buf, size, "%s%d.%d", t_tenths < 0 ? "-" : "", abs(t_tenths) / 10, abs(t_tenths) % 10);
    return buf;
}

/* One decision; `cmd` (may be NULL) is a button command applied first. */
static void control_step(sh_pump_ctl_t *pump, const sh_control_cmd_t *cmd)
{
    sh_state_t st;
    sh_state_get(&st);              /* the last values published by the measurement task */
    const uint32_t now = now_s();
    char tbuf[12];

    sh_settings_t set;
    sh_settings_get(&set);
    sh_installer_t inst;
    sh_limits_t lim;
    sh_installer_get(&inst, &lim);
    sh_clock_info_t ci;
    sh_clock_get(&ci);
    const bool reliable = ci.trust == SH_CLOCK_RELIABLE;
    /* a faulty sensor's last valid value is not used */
    const int16_t t_col = st.col_fault ? SH_TEMP_INVALID : st.t_col;
    const int16_t t_tank = st.tank_fault ? SH_TEMP_INVALID : st.t_tank;
    bool learned = false;

    /* sunrise and sunset, seen by the collector with the pump as the last step left it */
    const uint8_t sun = sh_sun_step(&s_sun, t_col, t_tank, pump->on, &ci.lt, reliable, now);
    if (sun & SH_SUN_EVT_RISE) {
        s_modes.sunrises++;
        ESP_LOGI(TAG, "sunrise detected: collector %s °C", temp_str(tbuf, sizeof(tbuf), t_col, false));
    }
    if (sun & SH_SUN_EVT_SET) {
        s_modes.sunsets++;
        ESP_LOGI(TAG, "sunset detected: collector %s °C", temp_str(tbuf, sizeof(tbuf), t_col, false));
    }
    learned = (sun & SH_SUN_EVT_LEARNED) != 0;

    /* the season: by hand, by the daylight saving time, or by the collector */
    bool winter = sh_settings_winter();
    const bool season = sh_season_step(&s_season, (sh_season_mode_t)set.v[SH_SET_SEASON], winter, t_col, pump->on,
                                       reliable && ci.tz_dst, ci.dst, now);
    if (season != winter) {
        winter = season;
        sh_settings_set_winter(winter);
        ESP_LOGI(TAG, "season: %s", winter ? "winter" : "summer");
    }

    sh_pump_params_t params;
    get_params(&params, &set, winter, &lim, &inst);
    const uint32_t test_s = (uint32_t)set.v[SH_SET_TEST_S];
    const bool vacation = params.charge_off;
    if (vacation != s_vacation) {
        s_vacation = vacation;
        sh_state_set_vacation(vacation);
        ESP_LOGI(TAG, "vacation %s%s%s", vacation ? "on: collector protection at " : "off",
                 vacation ? temp_str(tbuf, sizeof(tbuf), sh_pump_col_protect(&params), false) : "",
                 vacation ? " °C, no charging, cooling at night" : "");
    }

    /* night cooling: is a session active now? */
    const int16_t cool_target = sh_cool_target(inst.v[SH_INST_COL_TYPE] == SH_COL_TUBES, inst.v[SH_INST_COL_AREA],
                                               inst.v[SH_INST_TANK_VOL], params.tank_max, s_cool.offset);
    const sh_cool_in_t cin = {
        .reliable = reliable, .min = ci.lt.min, .t_col = t_col, .t_tank = t_tank,
        .tank_max = params.tank_max, .target = cool_target,
        .tmax_on = set.v[sh_settings_seasonal(SH_SET_NCOOL, winter)] != 0, .vacation = vacation,
        .e4 = (s_alarm.active & SH_ALARM_BIT(SH_ALARM_E4)) != 0,
        .night = sh_sun_night(&s_sun, &ci.lt, reliable), .dark = s_sun.dark, .sunset_s = s_sun.sunset_s,
        .pump_cool = pump->on && pump->reason == SH_PUMP_REASON_COOL,
    };
    const int8_t was_offset = s_cool.offset;
    sh_cool_step(&s_cool, &cin, now);
    static const char *const triggers[] = { "none", "tank above Tmax", "E4 during the day", "vacation" };
    static const char *const ends[] = { "none", "target reached", "end of the night", "heater running",
                                        "sensor fault", "vacation switched" };
    if (s_cool.events & SH_COOL_EVT_END) {
        ESP_LOGI(TAG, "night cooling ended (%s): tank %s °C", ends[s_cool.end],
                 temp_str(tbuf, sizeof(tbuf), t_tank, false));
    }
    if (s_cool.events & SH_COOL_EVT_START) {
        if (s_cool.trigger == SH_COOL_VACATION) {
            ESP_LOGI(TAG, "night cooling started (vacation): until the sunrise");
        } else {
            ESP_LOGI(TAG, "night cooling started (%s): target %s °C, until %s", triggers[s_cool.trigger],
                     temp_str(tbuf, sizeof(tbuf), cool_target, false), reliable ? "07:00" : "the sunrise");
        }
    }
    if (s_cool.offset != was_offset) {
        ESP_LOGI(TAG, "night cooling target: learned correction %d K", s_cool.offset);
    }
    params.cool = s_cool.trigger != SH_COOL_NONE;

    /* the heater: its sign (the tank with the pump stopped, kept by sh_pump_ctl), detection with "0 kW" */
    const int16_t power = inst.v[SH_INST_HEATER];
    const bool heater_on = sh_pump_ctl_heater(pump);
    if (sh_heater_step(&s_heater, power, heater_on)) {
        ESP_LOGW(TAG, "heater detected although the installer data say 0 kW: tank %s °C",
                 temp_str(tbuf, sizeof(tbuf), t_tank, false));
        learned = true;
    }

    /* the leak of the heater: checks with the pump stopped outside the guaranteed daylight */
    const int sun_rise = sh_sun_avg(s_sun.days.rise), sun_set = sh_sun_avg(s_sun.days.set);
    const sh_leak_in_t lin = {
        .heater = sh_heater_present(&s_heater, power), .reliable = reliable, .min = ci.lt.min,
        .rise = sun_rise, .set = sun_set, .t_col = t_col, .pump_on = pump->on,
        .charging = pump->on && pump->mode == SH_PUMP_MODE_NORMAL &&
                    (pump->reason == SH_PUMP_REASON_DIFF || pump->reason == SH_PUMP_REASON_MIN_RUN),
        .sunrise = (sun & SH_SUN_EVT_RISE) != 0,
    };
    sh_leak_step(&s_leak, &lin, now);
    if (s_leak.events & SH_LEAK_EVT_CHECK) {
        ESP_LOGI(TAG, "heater leak check: pump off for %u s, collector %s °C", SH_LEAK_CHECK_S,
                 temp_str(tbuf, sizeof(tbuf), t_col, false));
    }
    if (s_leak.events & (SH_LEAK_EVT_PASS | SH_LEAK_EVT_STOP)) {
        ESP_LOGI(TAG, "heater leak check: collector %s by %d.%d K -> %s", s_leak.last_drop >= 0 ? "fell" : "rose",
                 abs(s_leak.last_drop) / 10, abs(s_leak.last_drop) % 10,
                 s_leak.last_stop ? "leak of the heater stopped" : "the pump goes on");
    }
    if (s_leak.events & SH_LEAK_EVT_ABORT) {
        ESP_LOGI(TAG, "heater leak check given up");
    }
    if (s_leak.events & SH_LEAK_EVT_END) {
        ESP_LOGI(TAG, "heater leak stop ended: collector %s °C", temp_str(tbuf, sizeof(tbuf), t_col, false));
    }
    params.leak_check = s_leak.phase == SH_LEAK_CHECK;
    params.leak_stop = s_leak.phase == SH_LEAK_STOP;
    params.leak_resume = s_leak.resume;

    /* E8: reverse thermosiphon at night */
    const sh_e8_in_t ein = {
        .night = sh_sun_night(&s_sun, &ci.lt, reliable), .t_col = t_col, .t_tank = t_tank, .pump_on = pump->on,
        .heater = heater_on,
        .threshold = sh_e8_threshold(inst.v[SH_INST_COL_TYPE] == SH_COL_TUBES, inst.v[SH_INST_COL_AREA],
                                     inst.v[SH_INST_TANK_VOL]),
    };
    sh_e8_step(&s_e8, &ein, now);
    if (s_e8.events & SH_E8_EVT_SIGN) {
        ESP_LOGW(TAG, "reverse thermosiphon sign: tank fell %d.%d K in 1 h while the collector rose",
                 s_e8.last_rate / 10, abs(s_e8.last_rate) % 10);
    }
    if (s_e8.events & SH_E8_EVT_NIGHT) {
        ESP_LOGI(TAG, "night judged: %u night(s) with the reverse thermosiphon sign, %u normal night(s) in a row",
                 s_e8.l.sign_nights, s_e8.l.normal_nights);
    }
    if (s_e8.events & SH_E8_EVT_CHANGED) {
        learned = true;
    }
    if (learned || (s_cool.events & SH_COOL_EVT_LEARNED)) {
        save_learned();
    }

    /* the computed limits: at boot and whenever the installer data change them */
    static sh_pump_params_t s_logged = { .col_boiling = INT16_MIN };
    if (params.col_boiling != s_logged.col_boiling || params.col_protect_cap != s_logged.col_protect_cap ||
        params.frost != s_logged.frost) {
        s_logged = params;
        char boil[12], cap[12], frost[12];
        ESP_LOGI(TAG, "boiling point (no start) %s °C, collector protection ceiling %s °C, "
                 "frost threshold %s °C", temp_str(boil, sizeof(boil), params.col_boiling, false),
                 temp_str(cap, sizeof(cap), params.col_protect_cap, false),
                 temp_str(frost, sizeof(frost), params.frost, false));
        if (SH_CONTROL_COL_PROTECT > params.col_protect_cap) {
            ESP_LOGW(TAG, "collector protection %d °C is limited to %s °C", CONFIG_SH_COL_PROTECT_C, cap);
        }
    }

    const bool was_on = pump->on;
    const sh_pump_reason_t was_reason = pump->reason;
    const uint32_t idle_h = pump->on ? 0 : (now - pump->switched_s) / 3600;
    const bool was_test = pump->test;
    const uint32_t was_end = pump->test_end_s;

    if (cmd) {
        switch (*cmd) {
        case SH_CONTROL_TEST_PLUS:
            sh_pump_ctl_plus(pump, test_s, now);
            break;
        case SH_CONTROL_TEST_MINUS:
            sh_pump_ctl_minus(pump, now);
            break;
        case SH_CONTROL_TEST_STOP:
            sh_pump_ctl_stop(pump, now);
            break;
        case SH_CONTROL_AIR_START:
            sh_pump_ctl_air_start(pump, sh_air_pause_s(&inst), now);
            break;
        case SH_CONTROL_AIR_STOP:
            sh_pump_ctl_air_stop(pump);
            break;
        case SH_CONTROL_RESET_LEARNED:
            sh_sun_days_reset(&s_sun.days);
            s_sun.set_pending = false;
            s_cool.offset = 0;
            s_cool.quiet = 0;
            s_heater.detected = false;
            for (int i = 0; i < SH_E8_DAYS; i++) {
                s_e8.l.rate[i] = SH_E8_NO_RATE;       /* the E8 counters stay */
            }
            sh_learn_reset();
            ESP_LOGW(TAG, "learned values reset (sunrise / sunset, cooling target, detected heater, "
                     "normal night cooling)");
            break;
        }
        if (was_test && pump->test && pump->test_end_s != was_end) {
            ESP_LOGI(TAG, "pump test: %lu s left", (unsigned long)sh_pump_ctl_test_left(pump, now));
        }
    }

    const sh_pump_temps_t temps = {
        .t_col = st.t_col, .t_tank = st.t_tank, .col_fault = st.col_fault, .tank_fault = st.tank_fault,
    };
    sh_pump_ctl_step(pump, &params, &temps, now);
    sh_relay_set(pump->on);

    if (pump->events & SH_PUMP_EVT_TEST_START) {
        ESP_LOGI(TAG, "pump test started: %lu s", (unsigned long)sh_pump_ctl_test_left(pump, now));
    }
    if (pump->events & SH_PUMP_EVT_TEST_END) {
        ESP_LOGI(TAG, "pump test ended");
    }
    if (pump->events & SH_PUMP_EVT_AIR_START) {
        ESP_LOGI(TAG, "air venting started: %u cycles of %u s run + %lu s pause", SH_AIR_CYCLES, SH_AIR_RUN_S,
                 (unsigned long)pump->air_pause_s);
    }
    if (pump->events & SH_PUMP_EVT_AIR_END) {
        ESP_LOGI(TAG, "air venting ended");
    }
    if (pump->events & SH_PUMP_EVT_SEIZE_START) {
        ESP_LOGI(TAG, "anti-seize: pump on for %u s (%lu h without running)", SH_SEIZE_RUN_S,
                 (unsigned long)idle_h);
    }
    if (pump->events & SH_PUMP_EVT_SEIZE_END) {
        ESP_LOGI(TAG, "anti-seize run ended");
    }
    pump->events = 0;

    if (pump->test != was_test || pump->test_end_s != was_end) {
        sh_state_set_test(pump->test, pump->test_start_s, pump->test_end_s);
    }
    if (pump->on != was_on || pump->reason != was_reason) {
        sh_state_set_pump(pump->on, pump->reason);
        /* every switch, and every change of the reason (why the pump does not start or stop) */
        char col[12], tank[12];
        ESP_LOGI(TAG, "pump %s (%s): collector %s °C, tank %s °C", pump->on ? "ON" : "OFF",
                 sh_pump_reason_name(pump->reason), temp_str(col, sizeof(col), st.t_col, st.col_fault),
                 temp_str(tank, sizeof(tank), st.t_tank, st.tank_fault));
    }

    /* season, night cooling, learned values, air venting: published when they change */
    s_modes.winter = winter;
    s_modes.cool = (uint8_t)s_cool.trigger;
    s_modes.cool_target = cool_target;
    s_modes.cool_clock = reliable && s_cool.trigger != SH_COOL_VACATION;
    s_modes.cool_heater = s_cool.heater;
    s_modes.cool_learned = cool_target;
    s_modes.sunrise = (int16_t)sh_sun_avg(s_sun.days.rise);
    s_modes.sunset = (int16_t)sh_sun_avg(s_sun.days.set);
    s_modes.air_on = pump->air;
    s_modes.air_start_s = pump->air ? pump->air_start_s : 0;
    s_modes.air_pause_s = pump->air ? pump->air_pause_s : 0;
    s_modes.heater_on = heater_on;
    s_modes.heater_det = s_heater.detected;
    s_modes.leak = (uint8_t)s_leak.phase;
    s_modes.leak_start_s = s_leak.phase == SH_LEAK_CHECK ? s_leak.check_s : 0;
    s_modes.leak_last = s_leak.last;
    s_modes.leak_last_stop = s_leak.last_stop;
    s_modes.leak_last_drop = s_leak.last_drop;
    s_modes.e8_normal = sh_e8_normal(&s_e8.l);
    static sh_modes_t s_published;
    if (!s_modes_done || memcmp(&s_modes, &s_published, sizeof(s_modes)) != 0) {
        sh_state_set_modes(&s_modes);
        s_published = s_modes;
        s_modes_done = true;
    }

    /* alarm and information codes: every appearance and clearing in the log */
    const sh_alarm_in_t ain = {
        .t_col = st.t_col, .t_tank = st.t_tank, .col_fault = st.col_fault, .tank_fault = st.tank_fault,
        .pump_on = pump->on, .reason = pump->reason,
        .col_boiling = pump->norm.col_boiling, .tank_ceiling = pump->norm.tank_ceiling,
        .e8 = s_e8.l.active,
    };
    const uint8_t was_alarms = s_alarm.active;
    const uint8_t alarms = sh_alarm_step(&s_alarm, &ALARM_PARAMS, &ain, now);
    if (alarms != was_alarms) {
        sh_state_set_alarms(alarms);
        char col[12], tank[12];
        temp_str(col, sizeof(col), st.t_col, st.col_fault);
        temp_str(tank, sizeof(tank), st.t_tank, st.tank_fault);
        for (int a = 0; a < SH_ALARM_COUNT; a++) {
            const uint8_t bit = SH_ALARM_BIT(a);
            if ((alarms ^ was_alarms) & bit) {
                ESP_LOGW(TAG, "%s %s: collector %s °C, tank %s °C", sh_alarm_name((sh_alarm_t)a),
                         (alarms & bit) ? "active" : "cleared", col, tank);
            }
        }
    }

    /* status LED: computed only when its inputs change */
    sh_ind_in_t iin;
    memset(&iin, 0, sizeof(iin));   /* padding too: compared with memcmp() */
    iin.alarms = alarms;
    iin.pump_on = pump->on;
    iin.t_col = st.t_col;
    iin.col_fault = st.col_fault;
    iin.vacation = vacation;
    iin.cooling = pump->on && pump->reason == SH_PUMP_REASON_COOL;
    iin.wifi_connected = st.wifi_connected;
    iin.wifi_portal = st.wifi_portal;
    if (!s_ind_done || memcmp(&iin, &s_ind_in, sizeof(iin)) != 0) {
        const sh_ind_out_t out = sh_ind_update(&s_ind, &IND_PARAMS, &iin);
        sh_led_show(out.color, out.pattern);
        s_ind_in = iin;
        s_ind_done = true;
    }
}

static void control_task(void *arg)
{
    (void)arg;
    static sh_pump_ctl_t pump;      /* zeros: pump off since boot */
    sh_learned_t learned;
    sh_learn_load(&learned);        /* the learned sunrises / sunsets, cooling target, heater, E8 */
    s_sun.days = learned.days;
    s_cool.offset = learned.cool_offset;
    s_cool.quiet = learned.cool_quiet;
    s_heater.detected = learned.heater;     /* the heater detected with "0 kW" */
    s_e8.l = learned.e8;                    /* the normal night cooling, the E8 counters and E8 */
    TickType_t next = xTaskGetTickCount();

    for (;;) {
        control_step(&pump, NULL);
        next += pdMS_TO_TICKS(CONTROL_PERIOD_MS);
        /* until the next cycle: a button command is applied at once */
        for (;;) {
            const TickType_t now = xTaskGetTickCount();
            const TickType_t wait = (int32_t)(next - now) > 0 ? next - now : 0;
            sh_control_cmd_t cmd;
            if (xQueueReceive(s_cmds, &cmd, wait) != pdTRUE) {
                break;
            }
            control_step(&pump, &cmd);
        }
    }
}

void sh_control_cmd(sh_control_cmd_t cmd)
{
    if (s_cmds) {
        xQueueSend(s_cmds, &cmd, 0);
    }
}

esp_err_t sh_control_start(void)
{
    s_cmds = xQueueCreate(4, sizeof(sh_control_cmd_t));
    if (!s_cmds || xTaskCreate(control_task, "sh_control", 6144, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "control task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
