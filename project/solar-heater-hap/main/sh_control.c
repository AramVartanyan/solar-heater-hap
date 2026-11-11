#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_relay.h"
#include "sh_led.h"
#include "sh_pump_logic.h"
#include "sh_control.h"

static const char *TAG = "sh_control";

#define CONTROL_PERIOD_MS   1000

static const sh_pump_params_t s_params = {
    .diff_on     = SH_DIFF_ON,
    .diff_off    = SH_DIFF_OFF,
    .min_run_s   = SH_MIN_RUN_S,
    .min_off_s   = SH_MIN_OFF_S,
    .tank_max    = SH_TANK_MAX,
    .col_min     = SH_COL_MIN,
    .col_protect = SH_COL_PROTECT,
};

/* monotonic seconds since boot (not the time of day) */
static uint32_t now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/* tenths of °C as text ("-0.5", "---" for an invalid reading) */
static const char *fmt_temp(char *buf, size_t len, int16_t t)
{
    if (t == SH_TEMP_INVALID) {
        snprintf(buf, len, "---");
    } else {
        snprintf(buf, len, "%s%d.%d", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
    }
    return buf;
}

static void control_task(void *arg)
{
    (void)arg;
    sh_pump_state_t pump = { 0 };
    uint32_t changed_s = now_s();       /* boot counts as the last switch (pump off) */
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        sh_state_t st;
        sh_state_get(&st);              /* latest values published by the sensors task */

        bool was_on = pump.on;
        sh_pump_decide(&s_params, st.t_col, st.t_tank, now_s() - changed_s, &pump);
        if (pump.on != was_on) {
            changed_s = now_s();
            char col[12], tank[12];
            ESP_LOGI(TAG, "pump %s (%s), collector %s C, tank %s C", pump.on ? "ON" : "OFF",
                     sh_pump_reason_str(pump.reason), fmt_temp(col, sizeof(col), st.t_col),
                     fmt_temp(tank, sizeof(tank), st.t_tank));
        }
        sh_relay_set(pump.on);
        sh_state_set_pump(pump.on, pump.reason);

        sh_led_set(st.wifi_connected ? SH_COLOR_BLUE : SH_COLOR_OFF);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

esp_err_t sh_control_start(void)
{
    if (SH_COL_PROTECT > SH_PUMP_COL_PROTECT_CAP) {
        ESP_LOGW(TAG, "collector protection %d C limited to %d C", SH_COL_PROTECT / 10,
                 SH_PUMP_COL_PROTECT_CAP / 10);
    }
    if (xTaskCreate(control_task, "sh_control", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "control task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
