#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ds18b20.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_sensor_logic.h"
#include "sh_sensors.h"

static const char *TAG = "sh_sensors";

typedef struct {
    const char *name;
    int pin;
    bool started;           /* conversion started in this cycle */
    sh_sensor_t filter;     /* last valid value, failed measurements, fault */
} sensor_t;

static sensor_t s_sensor[2] = {
    { .name = "collector", .pin = SH_PIN_T_COL },
    { .name = "tank",      .pin = SH_PIN_T_TANK },
};

esp_err_t sh_sensors_init(void)
{
    for (int i = 0; i < 2; i++) {
        sh_sensor_init(&s_sensor[i].filter);
        bool parasite = ds18b20_is_parasite(s_sensor[i].pin);
        ESP_LOGI(TAG, "DS18B20 (ds18b20 %s) %s: GPIO%d%s", DS18B20_VERSION, s_sensor[i].name, s_sensor[i].pin,
                 parasite ? " (parasite power or no sensor)" : ", external power");
    }
    return ESP_OK;
}

/* The result of a conversion that has ended (`converted`), in tenths of °C. */
static bool read_tenths(const sensor_t *s, bool converted, int16_t *t)
{
    int16_t raw;
    bool ok = converted && ds18b20_read_raw(s->pin, DS18B20_ANY, &raw);
    if (ok && raw == DS18B20_RAW_POWER_ON) {
        /* +85.0 °C is also the power-on value of a sensor that lost power: measure once more */
        ok = ds18b20_measure(s->pin, DS18B20_ANY, true) && ds18b20_read_raw(s->pin, DS18B20_ANY, &raw);
    }
    if (ok) {
        *t = ds18b20_raw_to_tenths(raw);
    }
    return ok;
}

/* One measurement of a sensor whose conversion was started; logs only the changes. */
static void measure_one(sensor_t *s)
{
    const bool was_failing = s->filter.fails > 0;
    const bool was_fault = s->filter.fault;

    int16_t t1 = SH_TEMP_INVALID;
    const bool ok1 = read_tenths(s, s->started && ds18b20_wait(s->pin), &t1);
    if (!sh_sensor_first(&s->filter, ok1, t1)) {
        /* at once one more reading, from a new conversion */
        int16_t t2 = SH_TEMP_INVALID;
        const bool ok2 = read_tenths(s, ds18b20_measure(s->pin, DS18B20_ANY, true), &t2);
        sh_sensor_second(&s->filter, ok1, t1, ok2, t2);
    }

    if (s->filter.fault && !was_fault) {
        ESP_LOGE(TAG, "%s sensor FAULTY: %d measurements failed in a row", s->name, SH_SENSOR_FAULT_COUNT);
    } else if (s->filter.fails > 0 && !was_failing) {
        if (s->filter.value == SH_TEMP_INVALID) {
            ESP_LOGW(TAG, "%s sensor: measurement failed, no valid value yet", s->name);
        } else {
            ESP_LOGW(TAG, "%s sensor: measurement failed, the last valid value stays in use", s->name);
        }
    } else if (s->filter.fails == 0 && was_failing) {
        ESP_LOGI(TAG, "%s sensor OK", s->name);
    }
}

/* One measurement of both sensors (blocks up to ~0.8 s, longer when a reading is repeated);
 * publishes to sh_state. */
static void sensors_read(void)
{
    /* both start first, so the two conversions run in parallel */
    for (int i = 0; i < 2; i++) {
        s_sensor[i].started = ds18b20_measure(s_sensor[i].pin, DS18B20_ANY, false);
    }
    for (int i = 0; i < 2; i++) {
        measure_one(&s_sensor[i]);
    }
    sh_state_set_temps(s_sensor[0].filter.value, s_sensor[1].filter.value,
                       s_sensor[0].filter.fault, s_sensor[1].filter.fault);
}

static void sensors_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONFIG_SH_MEASURE_PERIOD_S * 1000));
        sensors_read();
    }
}

esp_err_t sh_sensors_start(void)
{
    /* the first measurement here, so the control task starts with real values */
    sensors_read();
    if (xTaskCreate(sensors_task, "sh_sensors", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "sensors task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
