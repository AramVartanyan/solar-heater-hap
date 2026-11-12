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
#include "sh_learned_store.h"
#include "sh_modes.h"
#include "sh_clock.h"
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

static QueueHandle_t s_cmds;
static sh_modes_t s_modes;
static sh_state_modes_t s_published;
static bool s_published_vac, s_published_done;

/* The menu settings of the season in use (whole K / °C), Kconfig "Control" (minutes, °C) -> tenths
 * and seconds, and the limits computed from the installer data. Read on every cycle: a change
 * applies at once. */
static void get_params(const sh_settings_t *s, const sh_limits_t *lim, bool winter, sh_pump_params_t *p)
{
    p->diff_on = (int16_t)(s->v[sh_settings_seasonal(SH_SET_DIFF_ON, winter)] * 10);
    p->diff_off = (int16_t)(s->v[sh_settings_seasonal(SH_SET_DIFF_OFF, winter)] * 10);
    p->min_run_s = CONFIG_SH_MIN_RUN_MIN * 60;
    p->min_off_s = CONFIG_SH_MIN_OFF_MIN * 60;
    p->tank_max = (int16_t)(s->v[SH_SET_TANK_MAX] * 10);
    p->col_min = (int16_t)(s->v[sh_settings_seasonal(SH_SET_COL_MIN, winter)] * 10);
    p->col_protect = SH_CONTROL_COL_PROTECT;
    p->col_protect_cap = lim->protect_cap;
    p->col_boiling = lim->boiling;
    p->frost_on = sh_settings_frost();
    p->frost = lim->frost;
    p->vacation = s->v[SH_SET_VACATION] != 0;
    p->vac_protect = lim->vac_protect;
    p->cool = false;
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

/* The log of the modes: the season, the start and end of a cooling session; the learned values and
 * the recognised season kept in NVS when they change. */
static void modes_log(const sh_modes_t *m, const sh_state_t *st, bool was_winter)
{
    char tank[12], goal[12];
    temp_str(tank, sizeof(tank), st->t_tank, st->tank_fault);
    if (m->changed & SH_MODES_CH_SEASON) {
        sh_settings_set_auto_winter(m->season.winter);
    }
    if (m->winter != was_winter) {
        ESP_LOGI(TAG, "season: %s", m->winter ? "winter" : "summer");
    }
    if (m->changed & SH_MODES_CH_LEARNED) {
        sh_learned_save(&m->learned);
    }
    if (m->sun_evt & SH_SUN_EVT_RISE) {
        ESP_LOGI(TAG, "sunrise (collector rises with the pump off)");
    }
    if (m->sun_evt & SH_SUN_EVT_SET) {
        ESP_LOGI(TAG, "sunset (collector below the tank)");
    }
    const sh_cool_t *c = &m->cool;
    if (c->events & SH_COOL_EVT_END) {
        ESP_LOGI(TAG, "night cooling ended (%s): %s, tank %s °C", sh_cool_why_name(c->ended),
                 sh_cool_end_name(c->end), tank);
    }
    if (c->events & SH_COOL_EVT_START) {
        if (c->why == SH_COOL_VACATION) {
            ESP_LOGI(TAG, "night cooling started (vacation): until equalisation, tank %s °C", tank);
        } else {
            ESP_LOGI(TAG, "night cooling started (%s): target %s °C, tank %s °C", sh_cool_why_name(c->why),
                     temp_str(goal, sizeof(goal), c->target, false), tank);
        }
    }
    if (c->events & SH_COOL_EVT_LEARNED) {
        ESP_LOGW(TAG, "E4 after a night with cooling: the cooling target is lowered (correction %d K)",
                 m->learned.cool.offset / 10);
    }
}

/* The state of the modes and of the air venting, published when it changes. */
static void publish_modes(const sh_pump_ctl_t *pump, bool vacation, uint32_t now)
{
    const sh_modes_t *m = &s_modes;
    sh_state_modes_t pm;
    memset(&pm, 0, sizeof(pm));     /* padding too: compared with memcmp() */
    pm.winter = m->winter;
    pm.cool_why = (uint8_t)m->cool.why;
    pm.cool_goal = m->cool.why != SH_COOL_NONE ? m->cool.target : SH_TEMP_INVALID;
    pm.cool_until = m->cool.why != SH_COOL_NONE ? m->cool.until : -1;
    pm.cool_heater = m->cool.heater;
    pm.cool_target = m->target;
    pm.cool_learned = m->learned.cool.offset;
    pm.sun_rise = m->sun.rise_min;
    pm.sun_set = m->sun.set_min;
    pm.avg_rise = sh_sun_list_avg(&m->learned.rise);
    pm.avg_set = sh_sun_list_avg(&m->learned.set);
    uint32_t left_s;
    pm.air = sh_pump_ctl_air(pump, now, &pm.air_cycle, &pm.air_run, &left_s);
    pm.air_left_s = left_s;
    pm.air_start_s = pm.air ? pump->air_start_s : 0;
    if (!s_published_done || vacation != s_published_vac || memcmp(&pm, &s_published, sizeof(pm)) != 0) {
        sh_state_set_modes(vacation, &pm);
        s_published = pm;
        s_published_vac = vacation;
        s_published_done = true;
    }
}

/* One decision; `cmd` (may be NULL) is a button command applied first. */
static void control_step(sh_pump_ctl_t *pump, const sh_control_cmd_t *cmd)
{
    sh_state_t st;
    sh_state_get(&st);              /* the last values published by the measurement task */
    sh_settings_t set;
    sh_settings_get(&set);
    sh_installer_t inst;
    sh_limits_t lim;
    sh_installer_get(&inst, &lim);
    const uint32_t test_s = (uint32_t)set.v[SH_SET_TEST_S];
    const uint32_t now = now_s();

    /* the modes: sunrise / sunset, the season, the night cooling, the vacation */
    sh_clock_info_t ci;
    sh_clock_get(&ci);
    const bool col_ok = !st.col_fault && st.t_col != SH_TEMP_INVALID;
    const bool tank_ok = !st.tank_fault && st.t_tank != SH_TEMP_INVALID;
    const sh_modes_in_t modes_in = {
        .t_col = col_ok ? st.t_col : SH_TEMP_INVALID, .t_tank = tank_ok ? st.t_tank : SH_TEMP_INVALID,
        .pump_on = pump->on,
        .charging = pump->on && (pump->reason == SH_PUMP_REASON_DIFF || pump->reason == SH_PUMP_REASON_MIN_RUN),
        .tank_max = pump->norm.tank_max, .e4 = (s_alarm.active & SH_ALARM_BIT(SH_ALARM_E4)) != 0,
        .set = &set, .inst = &inst,
        .clock_set = ci.lt.set, .reliable = ci.trust == SH_CLOCK_RELIABLE, .day = ci.lt.day, .min = ci.lt.min,
        .has_dst = ci.has_dst, .dst = ci.dst,
    };
    const bool was_winter = s_modes.winter;
    sh_modes_step(&s_modes, &modes_in, now);
    modes_log(&s_modes, &st, was_winter);

    sh_pump_params_t params;
    get_params(&set, &lim, s_modes.winter, &params);
    params.cool = s_modes.cool.why != SH_COOL_NONE;

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
        if (params.col_protect > params.col_protect_cap) {
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
            if (pump->air) {
                ESP_LOGI(TAG, "air venting: stop");
            }
            sh_pump_ctl_air_stop(pump, now);
            break;
        case SH_CONTROL_RESET_LEARNED:
            sh_modes_reset_learned(&s_modes);
            sh_learned_save(&s_modes.learned);
            ESP_LOGW(TAG, "learned values reset (sunrise / sunset, cooling target)");
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
    if (pump->events & SH_PUMP_EVT_SEIZE_START) {
        ESP_LOGI(TAG, "anti-seize: pump on for %u s (%lu h without running)", SH_SEIZE_RUN_S,
                 (unsigned long)idle_h);
    }
    if (pump->events & SH_PUMP_EVT_SEIZE_END) {
        ESP_LOGI(TAG, "anti-seize run ended");
    }
    if (pump->events & SH_PUMP_EVT_AIR_START) {
        ESP_LOGI(TAG, "air venting started: %d x (%lu s run + %lu s pause)", SH_AIR_CYCLES,
                 (unsigned long)SH_AIR_RUN_S, (unsigned long)pump->air_pause_s);
    }
    if (pump->events & SH_PUMP_EVT_AIR_END) {
        ESP_LOGI(TAG, "air venting ended");
    }
    pump->events = 0;
    publish_modes(pump, set.v[SH_SET_VACATION] != 0, now);

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

    /* alarm and information codes: every appearance and clearing in the log */
    const sh_alarm_in_t ain = {
        .t_col = st.t_col, .t_tank = st.t_tank, .col_fault = st.col_fault, .tank_fault = st.tank_fault,
        .pump_on = pump->on, .reason = pump->reason,
        .col_boiling = pump->norm.col_boiling, .tank_ceiling = pump->norm.tank_ceiling,
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
    iin.vacation = set.v[SH_SET_VACATION] != 0;
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
    sh_learned_t learned;
    sh_learned_load(&learned);
    sh_modes_init(&s_modes, sh_settings_auto_winter(), &learned, now_s());
    s_cmds = xQueueCreate(4, sizeof(sh_control_cmd_t));
    if (!s_cmds || xTaskCreate(control_task, "sh_control", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "control task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
