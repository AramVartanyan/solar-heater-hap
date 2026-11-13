#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sh_state.h"

static sh_state_t s_state;
static SemaphoreHandle_t s_lock;

void sh_state_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_state, 0, sizeof(s_state));
    s_state.t_col = SH_TEMP_INVALID;
    s_state.t_tank = SH_TEMP_INVALID;
    s_state.modes.sunrise = s_state.modes.sunset = -1;
}

void sh_state_get(sh_state_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
}

void sh_state_get_shown(sh_state_t *out)
{
    sh_state_get(out);
    if (out->modes.pulse) {
        out->t_col = out->modes.rel_col;
        out->t_tank = out->modes.rel_tank;
    }
}

void sh_state_set_temps(int16_t t_col, int16_t t_tank, bool col_fault, bool tank_fault)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.t_col = t_col;
    s_state.t_tank = t_tank;
    s_state.col_fault = col_fault;
    s_state.tank_fault = tank_fault;
    xSemaphoreGive(s_lock);
}

void sh_state_set_test(bool on, uint32_t start_s, uint32_t end_s)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.test_on = on;
    s_state.test_start_s = start_s;
    s_state.test_end_s = end_s;
    xSemaphoreGive(s_lock);
}

void sh_state_set_pump(bool on, sh_pump_reason_t reason)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.pump_on = on;
    s_state.pump_reason = reason;
    xSemaphoreGive(s_lock);
}

void sh_state_set_wifi(bool connected, bool portal)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.wifi_connected = connected;
    s_state.wifi_portal = portal;
    xSemaphoreGive(s_lock);
}

void sh_state_set_alarms(uint8_t alarms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.alarms = alarms;
    xSemaphoreGive(s_lock);
}

void sh_state_set_vacation(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.vacation = on;
    xSemaphoreGive(s_lock);
}

void sh_state_set_modes(const sh_modes_t *modes)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.modes = *modes;
    xSemaphoreGive(s_lock);
}

void sh_state_set_link(bool net_ap, bool fw_prompt, bool fw_wait, bool ota_running)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.net_ap = net_ap;
    s_state.fw_prompt = fw_prompt;
    s_state.fw_wait = fw_wait;
    s_state.ota_running = ota_running;
    xSemaphoreGive(s_lock);
}
