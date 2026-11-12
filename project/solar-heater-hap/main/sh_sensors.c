#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ds18b20.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_meas.h"
#include "sh_sensors.h"

static const char *TAG = "sh_sensors";

typedef struct {
    const char *name;
    int pin;
    bool started;           /* conversion started in this cycle */
    sh_meas_t meas;         /* last valid value, failed measurements, fault */
} sensor_t;

static sensor_t s_sensor[2] = {
    { .name = "collector", .pin = SH_PIN_T_COL },
    { .name = "tank",      .pin = SH_PIN_T_TANK },
};

esp_err_t sh_sensors_init(void)
{
    for (int i = 0; i < 2; i++) {
        sh_meas_init(&s_sensor[i].meas);
        bool parasite = ds18b20_is_parasite(s_sensor[i].pin);
        ESP_LOGI(TAG, "DS18B20 (ds18b20 %s) %s: GPIO%d%s", DS18B20_VERSION, s_sensor[i].name, s_sensor[i].pin,
                 parasite ? " (parasite power or no sensor)" : ", external power");
    }
    return ESP_OK;
}

/* Starts a conversion on the sensors marked in `which`; both run in parallel. */
static void start(const bool which[2])
{
    for (int i = 0; i < 2; i++) {
        if (which[i]) {
            s_sensor[i].started = ds18b20_measure(s_sensor[i].pin, DS18B20_ANY, false);
        }
    }
}

/* Result of the started conversion: false when the sensor does not answer or the CRC is wrong. */
static bool read_one(sensor_t *s, int16_t *t)
{
    int16_t raw;
    bool ok = s->started && ds18b20_wait(s->pin) && ds18b20_read_raw(s->pin, DS18B20_ANY, &raw);
    if (ok && raw == DS18B20_RAW_POWER_ON) {
        /* +85.0 °C is also the power-on value of a sensor that lost power: measure once more */
        ok = ds18b20_measure(s->pin, DS18B20_ANY, true) && ds18b20_read_raw(s->pin, DS18B20_ANY, &raw);
    }
    *t = ok ? ds18b20_raw_to_tenths(raw) : SH_TEMP_INVALID;
    return ok;
}

/* One measurement of both sensors (blocks up to ~0.8 s, ~1.6 s with a second reading); publishes
 * the last valid values and the faults to sh_state. */
static void sensors_read(void)
{
    const bool both[2] = { true, true };
    bool ok1[2], retry[2], success[2];
    int16_t t1[2], value[2];

    start(both);
    for (int i = 0; i < 2; i++) {
        ok1[i] = read_one(&s_sensor[i], &t1[i]);
        success[i] = sh_meas_accept(&s_sensor[i].meas, ok1[i], t1[i]);
        retry[i] = !success[i];
        value[i] = t1[i];
    }

    if (retry[0] || retry[1]) {
        start(retry);                   /* the second reading: a new conversion at once */
        for (int i = 0; i < 2; i++) {
            if (!retry[i]) {
                continue;
            }
            int16_t t2;
            bool ok2 = read_one(&s_sensor[i], &t2);
            success[i] = sh_meas_accept_retry(&s_sensor[i].meas, ok1[i], t1[i], ok2, t2);
            value[i] = t2;
        }
    }

    for (int i = 0; i < 2; i++) {
        sensor_t *s = &s_sensor[i];
        if (retry[i] && s->meas.fails < SH_MEAS_FAULT_COUNT) {
            if (!ok1[i]) {
                ESP_LOGW(TAG, "%s: no answer or read error, %s", s->name,
                         success[i] ? "second reading OK" : "second reading failed");
            } else {
                ESP_LOGW(TAG, "%s: implausible reading %d (0.1 °C), %s", s->name, t1[i],
                         success[i] ? "confirmed by the second reading" : "second reading failed");
            }
        }
        if (sh_meas_update(&s->meas, success[i], value[i])) {
            if (s->meas.faulty) {
                ESP_LOGE(TAG, "%s sensor FAULTY (%d failed measurements in a row)", s->name,
                         SH_MEAS_FAULT_COUNT);
            } else {
                ESP_LOGI(TAG, "%s sensor OK again", s->name);
            }
        }
    }
    sh_state_set_temps(s_sensor[0].meas.last, s_sensor[1].meas.last, s_sensor[0].meas.faulty,
                       s_sensor[1].meas.faulty);
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
