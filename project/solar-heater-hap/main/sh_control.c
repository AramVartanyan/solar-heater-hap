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
#include "sh_alarms.h"
#include "sh_settings_store.h"
#include "sh_relay.h"
#include "sh_led.h"
#include "sh_control.h"

static const char *TAG = "sh_control";

#define CONTROL_PERIOD_MS   1000

_Static_assert(CONFIG_SH_DIFF_OFF_K < CONFIG_SH_DIFF_ON_K,
               "Control: the switch-off difference must be lower than the switch-on difference");

_Static_assert(CONFIG_SH_E6_CLEAR_DIFF_K < CONFIG_SH_E6_DIFF_K,
               "Control: the difference that clears E6 must be lower than the one that declares it");

/* E6 thresholds: Kconfig "Control" (minutes, K, tenths of K) -> seconds and tenths. */
static const sh_alarm_params_t ALARM_PARAMS = {
    .window_s = CONFIG_SH_E6_WINDOW_MIN * 60,
    .col_rise = CONFIG_SH_E6_COL_RISE_K * 10,
    .tank_rise = CONFIG_SH_E6_TANK_RISE_DK,
    .diff = CONFIG_SH_E6_DIFF_K * 10,
    .clear_diff = CONFIG_SH_E6_CLEAR_DIFF_K * 10,
};

static const sh_led_params_t LED_PARAMS = {
    .cold = CONFIG_SH_LED_COLD_C * 10,
    .hot = CONFIG_SH_LED_HOT_C * 10,
};

/* Everything the control task keeps between the steps; all zeros at boot. */
typedef struct {
    sh_pump_ctl_t pump;         /* pump off since boot */
    sh_alarms_t alarms;
    sh_led_mem_t led_mem;
    sh_led_in_t led_in;         /* the inputs of the last LED decision */
    bool led_decided;
} control_t;

static QueueHandle_t s_cmds;

/* The menu settings (whole K / °C) and Kconfig "Control" (minutes, °C) -> tenths and seconds.
 * Read on every cycle: a changed setting applies at once. */
static void get_params(sh_pump_params_t *p, uint32_t *test_s)
{
    sh_settings_t s;
    sh_settings_get(&s);
    p->diff_on = (int16_t)(s.v[SH_SET_DIFF_ON] * 10);
    p->diff_off = (int16_t)(s.v[SH_SET_DIFF_OFF] * 10);
    p->min_run_s = CONFIG_SH_MIN_RUN_MIN * 60;
    p->min_off_s = CONFIG_SH_MIN_OFF_MIN * 60;
    p->tank_max = (int16_t)(s.v[SH_SET_TANK_MAX] * 10);
    p->col_min = (int16_t)(s.v[SH_SET_COL_MIN] * 10);
    p->col_protect = CONFIG_SH_COL_PROTECT_C * 10;
    *test_s = (uint32_t)s.v[SH_SET_TEST_S];
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

/* Alarms and information after the pump decision: every code that appears or clears is logged,
 * the active ones are published. */
static void alarms_step(control_t *c, const sh_state_t *st, uint32_t now)
{
    const uint8_t was = c->alarms.active;
    const sh_alarm_in_t in = {
        .t_col = st->t_col, .t_tank = st->t_tank, .col_fault = st->col_fault,
        .tank_fault = st->tank_fault, .pump_on = c->pump.on, .pump_reason = c->pump.reason,
    };
    const uint8_t active = sh_alarms_step(&c->alarms, &ALARM_PARAMS, &in, now);
    if (active == was) {
        return;
    }
    sh_state_set_alarms(active);
    char col_buf[12], tank_buf[12];
    const char *col = temp_str(col_buf, sizeof(col_buf), st->t_col, st->col_fault);
    const char *tank = temp_str(tank_buf, sizeof(tank_buf), st->t_tank, st->tank_fault);
    for (int code = 0; code < SH_ALARM_COUNT; code++) {
        const uint8_t bit = SH_ALARM_BIT(code);
        if (!((active ^ was) & bit)) {
            continue;
        }
        const char *name = sh_alarm_name((sh_alarm_t)code);
        const char *desc = sh_alarm_desc((sh_alarm_t)code);
        if (!(active & bit)) {
            ESP_LOGI(TAG, "%s cleared (%s): collector %s °C, tank %s °C", name, desc, col, tank);
        } else if (bit & SH_ALARMS_ERROR) {
            ESP_LOGW(TAG, "%s active (%s): collector %s °C, tank %s °C", name, desc, col, tank);
        } else {
            ESP_LOGI(TAG, "%s active (%s): collector %s °C, tank %s °C", name, desc, col, tank);
        }
    }
}

/* Status LED: decided again only when one of its inputs changed. */
static void led_step(control_t *c, const sh_state_t *st)
{
    sh_led_in_t in;
    memset(&in, 0, sizeof(in));     /* also the padding: the inputs are compared as bytes */
    in.alarms = c->alarms.active;
    in.pump_on = c->pump.on;
    in.vacation = st->vacation;
    in.t_col = st->col_fault ? SH_TEMP_INVALID : st->t_col;
    in.wifi_connected = st->wifi_connected;
    in.wifi_portal = st->wifi_portal;
    if (c->led_decided && memcmp(&in, &c->led_in, sizeof(in)) == 0) {
        return;
    }
    c->led_in = in;
    c->led_decided = true;
    sh_led_out_t out;
    sh_led_decide(&LED_PARAMS, &c->led_mem, &in, &out);
    sh_led_show(out.color, out.pattern);
}

/* One decision; `cmd` (may be NULL) is a button command applied first. */
static void control_step(control_t *c, const sh_control_cmd_t *cmd)
{
    sh_pump_ctl_t *pump = &c->pump;
    sh_state_t st;
    sh_state_get(&st);              /* the last values published by the measurement task */
    sh_pump_params_t params;
    uint32_t test_s;
    get_params(&params, &test_s);
    const uint32_t now = now_s();

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

    alarms_step(c, &st, now);
    led_step(c, &st);
}

static void control_task(void *arg)
{
    (void)arg;
    static control_t ctl;           /* zeros: pump off since boot, no alarm */
    TickType_t next = xTaskGetTickCount();

    for (;;) {
        control_step(&ctl, NULL);
        next += pdMS_TO_TICKS(CONTROL_PERIOD_MS);
        /* until the next cycle: a button command is applied at once */
        for (;;) {
            const TickType_t now = xTaskGetTickCount();
            const TickType_t wait = (int32_t)(next - now) > 0 ? next - now : 0;
            sh_control_cmd_t cmd;
            if (xQueueReceive(s_cmds, &cmd, wait) != pdTRUE) {
                break;
            }
            control_step(&ctl, &cmd);
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
#if CONFIG_SH_COL_PROTECT_C * 10 > SH_PUMP_COL_PROTECT_CAP
    ESP_LOGW(TAG, "collector protection %d °C is limited to %d °C", CONFIG_SH_COL_PROTECT_C,
             SH_PUMP_COL_PROTECT_CAP / 10);
#endif
    s_cmds = xQueueCreate(4, sizeof(sh_control_cmd_t));
    if (!s_cmds || xTaskCreate(control_task, "sh_control", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "control task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
