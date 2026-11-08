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
}

void sh_state_get(sh_state_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
}

void sh_state_set_temps(int16_t t_col, int16_t t_tank)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.t_col = t_col;
    s_state.t_tank = t_tank;
    xSemaphoreGive(s_lock);
}

void sh_state_set_pump(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.pump_on = on;
    xSemaphoreGive(s_lock);
}

void sh_state_set_wifi(bool connected, bool portal)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.wifi_connected = connected;
    s_state.wifi_portal = portal;
    xSemaphoreGive(s_lock);
}
