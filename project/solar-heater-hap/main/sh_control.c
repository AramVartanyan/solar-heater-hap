#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "sh_state.h"
#include "sh_pump_logic.h"
#include "sh_relay.h"
#include "sh_led.h"
#include "sh_control.h"

static const char *TAG = "sh_control";

#define CONTROL_PERIOD_MS   1000

_Static_assert(CONFIG_SH_DIFF_OFF_K < CONFIG_SH_DIFF_ON_K,
               "Control: the switch-off difference must be lower than the switch-on difference");

/* Kconfig "Control": whole K / °C / minutes -> tenths and seconds */
static const sh_pump_params_t s_params = {
    .diff_on = CONFIG_SH_DIFF_ON_K * 10,
    .diff_off = CONFIG_SH_DIFF_OFF_K * 10,
    .min_run_s = CONFIG_SH_MIN_RUN_MIN * 60,
    .min_off_s = CONFIG_SH_MIN_OFF_MIN * 60,
    .tank_max = CONFIG_SH_TANK_MAX_C * 10,
    .col_min = CONFIG_SH_COL_MIN_C * 10,
    .col_protect = CONFIG_SH_COL_PROTECT_C * 10,
};

/* monotonic seconds since boot (not the time of day) */
static uint32_t now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/* "23.4", "-0.5" or "---" for the log */
static const char *temp_str(char *buf, size_t size, int16_t t_tenths)
{
    if (t_tenths == SH_TEMP_INVALID) {
        return "---";
    }
    snprintf(buf, size, "%s%d.%d", t_tenths < 0 ? "-" : "", abs(t_tenths) / 10, abs(t_tenths) % 10);
    return buf;
}

static void control_task(void *arg)
{
    (void)arg;
    sh_pump_state_t pump = { 0 };
    uint32_t changed_s = now_s();       /* boot counts as the last switch: the pump is off */
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        sh_state_t st;
        sh_state_get(&st);              /* the last values published by the measurement task */

        const bool was_on = pump.on;
        const sh_pump_reason_t was_reason = pump.reason;
        sh_pump_decide(&s_params, &pump, st.t_col, st.t_tank, now_s() - changed_s);
        sh_relay_set(pump.on);

        if (pump.on != was_on || pump.reason != was_reason) {
            if (pump.on != was_on) {
                changed_s = now_s();
            }
            sh_state_set_pump(pump.on, pump.reason);
            /* every switch, and every change of the reason (why the pump does not start or stop) */
            char col[12], tank[12];
            ESP_LOGI(TAG, "pump %s (%s): collector %s °C, tank %s °C", pump.on ? "ON" : "OFF",
                     sh_pump_reason_name(pump.reason), temp_str(col, sizeof(col), st.t_col),
                     temp_str(tank, sizeof(tank), st.t_tank));
        }

        sh_led_set(st.wifi_connected ? SH_COLOR_BLUE : SH_COLOR_OFF);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

esp_err_t sh_control_start(void)
{
#if CONFIG_SH_COL_PROTECT_C * 10 > SH_PUMP_COL_PROTECT_CAP
    ESP_LOGW(TAG, "collector protection %d °C is limited to %d °C", CONFIG_SH_COL_PROTECT_C,
             SH_PUMP_COL_PROTECT_CAP / 10);
#endif
    if (xTaskCreate(control_task, "sh_control", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "control task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
