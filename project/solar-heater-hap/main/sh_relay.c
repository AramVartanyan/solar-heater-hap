#include "esp_log.h"
#include "outputwrite.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_relay.h"

static const char *TAG = "sh_relay";
static bool s_on;

esp_err_t sh_relay_init(void)
{
    esp_err_t err = ioInitPdown(SH_PIN_RELAY, true, true);
    OutputWrite(false, SH_PIN_RELAY, false);
    s_on = false;
    return err;
}

void sh_relay_set(bool on)
{
    if (on == s_on) {
        return;
    }
    OutputWrite(on, SH_PIN_RELAY, false);
    s_on = on;
    sh_state_set_pump(on);
    ESP_LOGI(TAG, "pump %s", on ? "ON" : "OFF");
}

bool sh_relay_get(void)
{
    return s_on;
}
