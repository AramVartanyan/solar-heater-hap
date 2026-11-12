#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "sh_state.h"
#include "sh_settings.h"
#include "sh_pump_logic.h"
#include "sh_relay.h"
#include "sh_led.h"
#include "sh_control.h"

static const char *TAG = "sh_control";

#define CONTROL_PERIOD_MS   1000

/* The pump state belongs to the control task; a key on the normal screen (display task) changes it
 * too, so both take this lock. */
static sh_pump_state_t s_pump;
static SemaphoreHandle_t s_lock;

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

/* The settings in use (whole °C / K / s) and Kconfig "Control" -> tenths and seconds. Read on
 * every cycle, so a change in the menu applies at once. */
static void params_get(sh_pump_params_t *p)
{
    sh_settings_t set;
    sh_settings_get(&set);
    p->diff_on = set.v[SH_SET_DIFF_ON] * 10;
    p->diff_off = set.v[SH_SET_DIFF_OFF] * 10;
    p->min_run_s = CONFIG_SH_MIN_RUN_MIN * 60;
    p->min_off_s = CONFIG_SH_MIN_OFF_MIN * 60;
    p->tank_max = set.v[SH_SET_TANK_MAX] * 10;
    p->col_min = set.v[SH_SET_COL_MIN] * 10;
    p->col_protect = CONFIG_SH_COL_PROTECT_C * 10;
    p->test_s = (uint16_t)set.v[SH_SET_TEST_S];
}

static void input_get(sh_pump_input_t *in, const sh_state_t *st)
{
    in->t_col = st->t_col;
    in->t_tank = st->t_tank;
    in->col_fault = st->col_fault;
    in->tank_fault = st->tank_fault;
}

static const char *forced_name(sh_pump_forced_t forced)
{
    return forced == SH_PUMP_FORCED_TEST ? "pump test" : "anti-seize run";
}

/* One decision: relay, shared state, log. Called with the lock held. */
static void control_step(const sh_pump_params_t *p, const sh_state_t *st)
{
    sh_pump_input_t in;
    input_get(&in, st);

    const bool was_on = s_pump.on;
    const sh_pump_reason_t was_reason = s_pump.reason;
    const sh_pump_forced_t was_forced = s_pump.forced;
    const uint32_t now = now_s();
    sh_pump_decide(p, &s_pump, &in, now);
    sh_relay_set(s_pump.on);

    const uint32_t test_left_s = sh_pump_test_left(&s_pump, now);
    sh_state_set_test(s_pump.forced == SH_PUMP_FORCED_TEST, (uint16_t)test_left_s);

    if (s_pump.forced != was_forced) {
        if (was_forced != SH_PUMP_FORCED_NONE) {
            ESP_LOGI(TAG, "%s ended", forced_name(was_forced));
        }
        if (s_pump.forced == SH_PUMP_FORCED_TEST) {
            ESP_LOGI(TAG, "pump test started: %u s", (unsigned)test_left_s);
        } else if (s_pump.forced == SH_PUMP_FORCED_ANTI_SEIZE) {
            ESP_LOGI(TAG, "anti-seize run started: %d s", SH_PUMP_SEIZE_RUN_S);
        }
    }
    if (s_pump.on != was_on || s_pump.reason != was_reason) {
        sh_state_set_pump(s_pump.on, s_pump.reason);
        /* every switch, and every change of the reason (why the pump does not start or stop) */
        char col[12], tank[12];
        ESP_LOGI(TAG, "pump %s (%s): collector %s °C, tank %s °C", s_pump.on ? "ON" : "OFF",
                 sh_pump_reason_name(s_pump.reason), temp_str(col, sizeof(col), st->t_col, st->col_fault),
                 temp_str(tank, sizeof(tank), st->t_tank, st->tank_fault));
    }
}

static void control_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        sh_state_t st;
        sh_state_get(&st);              /* the last values published by the measurement task */
        sh_pump_params_t params;
        params_get(&params);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        control_step(&params, &st);
        xSemaphoreGive(s_lock);

        sh_led_set(st.wifi_connected ? SH_COLOR_BLUE : SH_COLOR_OFF);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

bool sh_control_key(sh_key_t key)
{
    if (!s_lock) {
        return false;                   /* the control has not started yet */
    }
    sh_state_t st;
    sh_state_get(&st);
    sh_pump_params_t params;
    params_get(&params);
    sh_pump_input_t in;
    input_get(&in, &st);

    bool taken;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint32_t now = now_s();
    switch (key) {
    case SH_KEY_PLUS:
        sh_pump_test_plus(&params, &s_pump, &in, now);
        taken = true;
        break;
    case SH_KEY_MINUS:
        taken = sh_pump_test_minus(&s_pump, now);
        break;
    case SH_KEY_MENU:
        taken = sh_pump_test_stop(&s_pump, now);
        break;
    default:
        taken = false;                  /* auto-repeat is for the values in the menu */
        break;
    }
    if (taken) {
        control_step(&params, &st);     /* the pump and the time on the display follow at once */
    }
    xSemaphoreGive(s_lock);
    return taken;
}

esp_err_t sh_control_start(void)
{
#if CONFIG_SH_COL_PROTECT_C * 10 > SH_PUMP_COL_PROTECT_CAP
    ESP_LOGW(TAG, "collector protection %d °C is limited to %d °C", CONFIG_SH_COL_PROTECT_C,
             SH_PUMP_COL_PROTECT_CAP / 10);
#endif
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock || xTaskCreate(control_task, "sh_control", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "control task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
