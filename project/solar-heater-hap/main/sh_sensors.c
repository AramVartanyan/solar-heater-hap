#include <stdbool.h>
#include "esp_log.h"
#include "ds18b20.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_sensors.h"

static const char *TAG = "sh_sensors";

typedef struct {
    const char *name;
    int pin;
    bool ok;                /* last read succeeded (logs only the changes) */
    bool started;           /* conversion started in this cycle */
} sensor_t;

static sensor_t s_sensor[2] = {
    { "collector", SH_PIN_T_COL,  false, false },
    { "tank",      SH_PIN_T_TANK, false, false },
};

esp_err_t sh_sensors_init(void)
{
    for (int i = 0; i < 2; i++) {
        bool parasite = ds18b20_is_parasite(s_sensor[i].pin);
        ESP_LOGI(TAG, "DS18B20 %s: GPIO%d%s", s_sensor[i].name, s_sensor[i].pin,
                 parasite ? " (parasite power or no sensor)" : ", external power");
    }
    return ESP_OK;
}

static int16_t read_one(sensor_t *s)
{
    int16_t raw;
    bool ok = s->started && ds18b20_wait(s->pin) && ds18b20_read_raw(s->pin, DS18B20_ANY, &raw);
    if (ok && raw == DS18B20_RAW_POWER_ON) {
        /* +85.0 °C is also the power-on value of a sensor that lost power: measure once more */
        ok = ds18b20_measure(s->pin, DS18B20_ANY, true) && ds18b20_read_raw(s->pin, DS18B20_ANY, &raw);
    }
    if (ok != s->ok) {
        s->ok = ok;
        if (ok) {
            ESP_LOGI(TAG, "%s sensor OK", s->name);
        } else {
            ESP_LOGW(TAG, "%s sensor: no answer or read error", s->name);
        }
    }
    return ok ? ds18b20_raw_to_tenths(raw) : SH_TEMP_INVALID;
}

void sh_sensors_read(void)
{
    /* both start first, so the two conversions run in parallel */
    for (int i = 0; i < 2; i++) {
        s_sensor[i].started = ds18b20_measure(s_sensor[i].pin, DS18B20_ANY, false);
    }
    int16_t t_col = read_one(&s_sensor[0]);
    int16_t t_tank = read_one(&s_sensor[1]);
    sh_state_set_temps(t_col, t_tank);
}
