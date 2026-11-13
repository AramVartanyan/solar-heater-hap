#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>
#include <hap_platform_httpd.h>
#include "sh_state.h"
#include "sh_settings_store.h"
#include "sh_led.h"
#include "sh_reset.h"
#include "sh_net.h"
#include "sh_web.h"
#include "sh_buttons.h"
#include "sh_hap_rules.h"
#include "sh_hap.h"

static const char *TAG = "sh_hap";

#define NVS_NAMESPACE   "sh_hap"
#define KEY_ON          "on"            /* the setting "Apple HomeKit" */
#define KEY_PAIRED      "paired"        /* a controller was paired when HAP last ran */
#define KEY_UNITS       "units"         /* Temperature Display Units: 1 = °F on the display */
#define KEY_LOCK        "lock"          /* Lock Physical Controls */

#define MANUFACTURER    "Armo Ltd."
#define MODEL           "Solar Heater"
#define PUSH_MS         1000            /* the notifications: what has changed is looked for this often */

typedef enum {
    MSG_START,                  /* start HAP */
    MSG_EVENT,                  /* arg: sh_hap_evt_t */
} msg_type_t;

typedef struct {
    uint8_t type;
    uint8_t arg;
} msg_t;

typedef enum {
    HAP_OFF = 0,                /* the setting is off, or HAP could not start */
    HAP_IDLE,                   /* waiting for the first connection to the router */
    HAP_STARTING,
    HAP_RUNNING,
} hap_phase_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;        /* everything below, and the characteristics */
static bool s_on;                       /* the stored setting */
static volatile hap_phase_t s_phase;
static bool s_paired, s_window, s_lock_on, s_fahrenheit;
static sh_hap_view_t s_sent;            /* what the controllers have */

static hap_char_t *c_active, *c_cur_state, *c_target, *c_temp, *c_threshold, *c_units, *c_lock;
static hap_char_t *c_col_temp, *c_col_fault, *c_tank_temp, *c_tank_fault;

/* ---- storage ---------------------------------------------------------------------------- */

static void store(const char *key, bool value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s = %d not saved: %s", key, value, esp_err_to_name(err));
    }
}

/* s_lock held */
static void publish(void)
{
    sh_state_set_hap(s_window, s_lock_on, s_fahrenheit);
}

/* The keyboard lock from any source: stored, published, shown on the display; the notification to
 * HomeKit follows from push() (or from the write of the controller itself). s_lock held. */
static void lock_set(bool on, const char *source)
{
    if (!sh_hap_lock_change(&s_lock_on, on)) {
        return;
    }
    ESP_LOGI(TAG, "keyboard %s (%s)", on ? "locked" : "unlocked", source);
    store(KEY_LOCK, on);
    publish();
    sh_buttons_lock_changed(on);
}

/* The display units from Apple Home, or back to °C by "Default settings": stored, published (the
 * display and the web interface); the notification as for the lock. s_lock held. */
static void units_set(bool fahrenheit, const char *source)
{
    if (fahrenheit == s_fahrenheit) {
        return;
    }
    ESP_LOGI(TAG, "display units °%c (%s)", fahrenheit ? 'F' : 'C', source);
    s_fahrenheit = fahrenheit;
    store(KEY_UNITS, fahrenheit);
    publish();
}

/* ---- the values ------------------------------------------------------------------------- */

/* s_lock held */
static void view_now(sh_hap_view_t *v)
{
    sh_state_t st;
    sh_state_get(&st);
    sh_settings_t set;
    sh_settings_get(&set);
    const sh_hap_in_t in = {
        .vacation = set.v[SH_SET_VACATION] != 0, .tmax = set.v[SH_SET_TANK_MAX],
        .pump_on = st.pump_on, .reason = st.pump_reason, .alarms = st.alarms, .lock = s_lock_on,
        .fahrenheit = s_fahrenheit,
    };
    sh_hap_view(&in, v);
}

static void set_u8(hap_char_t *hc, uint8_t value)
{
    hap_val_t val = { .u = value };
    hap_char_update_val(hc, &val);      /* notifies the controllers that asked for it */
}

static void set_float(hap_char_t *hc, float value)
{
    hap_val_t val = { .f = value };
    hap_char_update_val(hc, &val);
}

/* Sends what has changed since the last time (menu, web interface, the control). */
static void push(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    sh_hap_view_t now;
    view_now(&now);
    const unsigned ch = sh_hap_changed(&s_sent, &now);
    if (ch & SH_HAP_CH_ACTIVE) {
        set_u8(c_active, now.active);
    }
    if (ch & SH_HAP_CH_CUR_STATE) {
        set_u8(c_cur_state, now.cur_state);
    }
    if (ch & SH_HAP_CH_THRESHOLD) {
        set_float(c_threshold, now.threshold);
    }
    if (ch & SH_HAP_CH_LOCK) {
        set_u8(c_lock, now.lock);
    }
    if (ch & SH_HAP_CH_COL_FAULT) {
        set_u8(c_col_fault, now.col_fault);
    }
    if (ch & SH_HAP_CH_TANK_FAULT) {
        set_u8(c_tank_fault, now.tank_fault);
    }
    if (ch & SH_HAP_CH_UNITS) {
        set_u8(c_units, now.units);
    }
    s_sent = now;
    xSemaphoreGive(s_lock);
}

/* The temperature of the collector or of the tank for HomeKit, °C: in the pulse mode the last
 * reliable value, with a faulty sensor its last valid one; false: no value yet. */
static bool temp_now(bool col, float *out)
{
    sh_state_t st;
    sh_state_get_shown(&st);
    int16_t t;
    if (!sh_hap_temp(col ? st.t_col : st.t_tank, col ? SH_HAP_COL_MIN : SH_HAP_TANK_MIN,
                     col ? SH_HAP_COL_MAX : SH_HAP_TANK_MAX, &t)) {
        return false;
    }
    *out = t / 10.0f;
    return true;
}

/* The temperatures: answered when a controller reads them, never sent on their own. */
static int temp_read(hap_char_t *hc, hap_status_t *status, void *serv_priv, void *read_priv)
{
    (void)serv_priv;
    (void)read_priv;
    float t;
    if ((hc == c_temp || hc == c_tank_temp || hc == c_col_temp) && temp_now(hc == c_col_temp, &t)) {
        set_float(hc, t);
    }
    *status = HAP_STATUS_SUCCESS;
    return HAP_SUCCESS;
}

/* A controller writes: Active (the vacation), Tmax, the display units, the keyboard lock. */
static int heater_write(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv)
{
    (void)serv_priv;
    (void)write_priv;
    int ret = HAP_SUCCESS;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < count; i++) {
        hap_write_data_t *w = &write_data[i];
        bool ok = false, flag = false;
        int16_t tmax = 0;
        if (w->hc == c_active) {
            ok = sh_hap_write_active((int)w->val.u, &flag) &&
                 sh_settings_set(SH_SET_VACATION, flag) == ESP_OK;
            if (ok) {
                ESP_LOGI(TAG, "HomeKit: Active %u — vacation %s", (unsigned)w->val.u, flag ? "on" : "off");
                s_sent.active = !flag;
            }
        } else if (w->hc == c_threshold) {
            ok = sh_hap_write_threshold(w->val.f, &tmax) && sh_settings_set(SH_SET_TANK_MAX, tmax) == ESP_OK;
            if (ok) {
                ESP_LOGI(TAG, "HomeKit: Heating Threshold — Tmax %d °C", tmax);
                w->val.f = tmax;            /* the whole degree that was taken */
                s_sent.threshold = tmax;
            }
        } else if (w->hc == c_target) {
            ok = sh_hap_write_target((int)w->val.u);
        } else if (w->hc == c_units) {
            ok = sh_hap_write_units((int)w->val.u, &flag);
            if (ok) {
                units_set(flag, "Apple Home");
                s_sent.units = flag ? SH_HAP_UNITS_F : SH_HAP_UNITS_C;
            }
        } else if (w->hc == c_lock) {
            ok = sh_hap_write_lock((int)w->val.u, &flag);
            if (ok) {
                lock_set(flag, "Apple Home");
                s_sent.lock = flag;
            }
        }
        if (ok) {
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;
        } else {
            *(w->status) = HAP_STATUS_VAL_INVALID;
            ret = HAP_FAIL;
        }
    }
    xSemaphoreGive(s_lock);
    return ret;
}

static int identify(hap_acc_t *ha)
{
    (void)ha;
    ESP_LOGI(TAG, "HomeKit: identify");
    sh_led_identify();
    return HAP_SUCCESS;
}

/* Runs in the tasks of esp-homekit-sdk (HTTP server, timer): only queues the event. */
static void hap_event(hap_event_t event, void *data)
{
    (void)data;
    msg_t m = { .type = MSG_EVENT };
    switch (event) {
    case HAP_EVENT_CTRL_PAIRED:
        m.arg = SH_HAP_EVT_PAIRED;
        break;
    case HAP_EVENT_CTRL_UNPAIRED:
        m.arg = SH_HAP_EVT_UNPAIRED;
        break;
    case HAP_EVENT_PAIRING_MODE_TIMED_OUT:
        m.arg = SH_HAP_EVT_TIMED_OUT;
        break;
    default:
        return;
    }
    xQueueSend(s_queue, &m, 0);
}

/* The pairing window and "paired" after an event. */
static void on_event(sh_hap_evt_t evt)
{
    const int count = hap_get_paired_controller_count();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool was_window = s_window;
    s_window = sh_hap_window(s_window, evt, count);
    if ((count > 0) != s_paired) {
        s_paired = count > 0;
        store(KEY_PAIRED, s_paired);
    }
    publish();
    const bool window = s_window;
    xSemaphoreGive(s_lock);
    if (window != was_window || evt != SH_HAP_EVT_TIMED_OUT) {
        ESP_LOGI(TAG, "HomeKit: %d controller(s) paired, the pairing window is %s", count,
                 window ? "open (10 min)" : "closed");
    }
}

/* ---- the accessory ---------------------------------------------------------------------- */

static hap_serv_t *sensor(const char *name, bool col, uint8_t fault, hap_char_t **temp, hap_char_t **status)
{
    float t = 0.0f;
    temp_now(col, &t);
    hap_serv_t *hs = hap_serv_temperature_sensor_create(t);
    if (!hs) {
        return NULL;
    }
    *temp = hap_serv_get_char_by_uuid(hs, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    hap_char_float_set_constraints(*temp, (col ? SH_HAP_COL_MIN : SH_HAP_TANK_MIN) / 10.0f,
                                   (col ? SH_HAP_COL_MAX : SH_HAP_TANK_MAX) / 10.0f, 0.1f);
    *status = hap_char_status_fault_create(fault);
    if (hap_serv_add_char(hs, hap_char_name_create((char *)name)) != HAP_SUCCESS || !*status ||
        hap_serv_add_char(hs, *status) != HAP_SUCCESS) {
        hap_serv_delete(hs);
        return NULL;
    }
    hap_serv_set_read_cb(hs, temp_read);
    return hs;
}

/* hap_init(), the accessory, hap_start(). */
static bool start(void)
{
    hap_register_event_handler(hap_event);
    if (hap_init(HAP_TRANSPORT_WIFI) != HAP_SUCCESS) {
        ESP_LOGE(TAG, "hap_init failed");
        return false;
    }
    hap_acc_cfg_t cfg = {
        .name = (char *)sh_net_name(),
        .manufacturer = MANUFACTURER,
        .model = MODEL,
        .serial_num = (char *)sh_net_serial_number(),
        .fw_rev = CONFIG_APP_PROJECT_VER,
        .pv = "1.1.0",
        .identify_routine = identify,
        .cid = HAP_CID_HEATER,
    };
    hap_acc_t *acc = hap_acc_create(&cfg);
    if (!acc) {
        ESP_LOGE(TAG, "accessory not created");
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    view_now(&s_sent);
    const sh_hap_view_t v = s_sent;
    const bool fahrenheit = s_fahrenheit;
    xSemaphoreGive(s_lock);

    /* Heater Cooler (primary) */
    float t_tank = 0.0f;
    temp_now(false, &t_tank);
    hap_serv_t *heater = hap_serv_heater_cooler_create(v.active, t_tank, v.cur_state, SH_HAP_TARGET_HEAT);
    if (!heater) {
        ESP_LOGE(TAG, "Heater Cooler not created");
        return false;
    }
    c_active = hap_serv_get_char_by_uuid(heater, HAP_CHAR_UUID_ACTIVE);
    c_temp = hap_serv_get_char_by_uuid(heater, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    c_cur_state = hap_serv_get_char_by_uuid(heater, HAP_CHAR_UUID_CURRENT_HEATER_COOLER_STATE);
    c_target = hap_serv_get_char_by_uuid(heater, HAP_CHAR_UUID_TARGET_HEATER_COOLER_STATE);
    c_threshold = hap_char_heating_threshold_temperature_create(v.threshold);
    c_units = hap_char_temperature_display_units_create(fahrenheit ? SH_HAP_UNITS_F : SH_HAP_UNITS_C);
    c_lock = hap_char_lock_physical_controls_create(v.lock);
    if (!c_threshold || !c_units || !c_lock) {
        ESP_LOGE(TAG, "characteristics not created");
        return false;
    }
    static const uint8_t cur_states[] = { SH_HAP_IDLE, SH_HAP_HEATING, SH_HAP_COOLING };
    static const uint8_t target_states[] = { SH_HAP_TARGET_HEAT };
    hap_char_int_set_constraints(c_cur_state, SH_HAP_IDLE, SH_HAP_COOLING, 1);
    hap_char_add_valid_vals(c_cur_state, cur_states, sizeof(cur_states));
    hap_char_int_set_constraints(c_target, SH_HAP_TARGET_HEAT, SH_HAP_TARGET_HEAT, 1);
    hap_char_add_valid_vals(c_target, target_states, sizeof(target_states));
    hap_char_float_set_constraints(c_temp, SH_HAP_TANK_MIN / 10.0f, SH_HAP_TANK_MAX / 10.0f, 0.1f);
    hap_char_float_set_constraints(c_threshold, sh_setting_info[SH_SET_TANK_MAX].min,
                                   sh_setting_info[SH_SET_TANK_MAX].max, sh_setting_info[SH_SET_TANK_MAX].step);
    hap_serv_add_char(heater, c_threshold);
    hap_serv_add_char(heater, hap_char_name_create(cfg.name));
    hap_serv_add_char(heater, c_units);
    hap_serv_add_char(heater, c_lock);
    hap_serv_set_read_cb(heater, temp_read);
    hap_serv_set_write_cb(heater, heater_write);
    hap_serv_mark_primary(heater);
    hap_acc_add_serv(acc, heater);

    hap_serv_t *col = sensor("Collector", true, v.col_fault, &c_col_temp, &c_col_fault);
    hap_serv_t *tank = sensor("Tank", false, v.tank_fault, &c_tank_temp, &c_tank_fault);
    if (!col || !tank) {
        ESP_LOGE(TAG, "temperature sensors not created");
        return false;
    }
    hap_acc_add_serv(acc, col);
    hap_acc_add_serv(acc, tank);
    hap_add_accessory(acc);

#ifdef CONFIG_SH_HAP_FIXED_SETUP_CODE
    /* development only: the product reads the setup info from factory_nvs */
    hap_set_setup_code(CONFIG_SH_HAP_SETUP_CODE);
    hap_set_setup_id(CONFIG_SH_HAP_SETUP_ID);
    ESP_LOGW(TAG, "development build: fixed HomeKit setup code %s", CONFIG_SH_HAP_SETUP_CODE);
#endif

    if (hap_start() != HAP_SUCCESS) {
        ESP_LOGE(TAG, "hap_start failed (no setup code in factory_nvs?)");
        return false;
    }
    return true;
}

static void hap_task(void *arg)
{
    (void)arg;
    for (;;) {
        msg_t m;
        const bool got = xQueueReceive(s_queue, &m, pdMS_TO_TICKS(PUSH_MS)) == pdTRUE;
        if (got && m.type == MSG_START && s_phase == HAP_STARTING) {
            sh_web_stop_wait();         /* the own server may still be up after the own access point */
            if (start()) {
                /* the pages on the server of HAP; the name in the mDNS that HAP has started */
                sh_web_attach(*hap_platform_httpd_get_handle());
                sh_net_mdns_start(false);
                s_phase = HAP_RUNNING;
                ESP_LOGI(TAG, "HomeKit started as %s", sh_net_name());
                on_event(SH_HAP_EVT_STARTED);
            } else {
                s_phase = HAP_OFF;      /* until the next boot: the own server and mDNS */
                ESP_LOGE(TAG, "HomeKit not started: the web interface stays on its own server");
                sh_net_hap_failed();
            }
        } else if (got && m.type == MSG_EVENT && s_phase == HAP_RUNNING) {
            on_event((sh_hap_evt_t)m.arg);
        }
        if (s_phase == HAP_RUNNING) {
            push();
        }
    }
}

/* ---- API -------------------------------------------------------------------------------- */

bool sh_hap_enabled(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool on = s_on;
    xSemaphoreGive(s_lock);
    return on;
}

bool sh_hap_wanted(void)
{
    return s_phase != HAP_OFF;
}

bool sh_hap_started(void)
{
    return s_phase >= HAP_STARTING;
}

bool sh_hap_running(void)
{
    return s_phase == HAP_RUNNING;
}

bool sh_hap_paired(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool paired = s_paired;
    xSemaphoreGive(s_lock);
    return paired;
}

void sh_hap_start(void)
{
    if (s_phase != HAP_IDLE) {
        return;
    }
    s_phase = HAP_STARTING;
    const msg_t m = { .type = MSG_START };
    xQueueSend(s_queue, &m, portMAX_DELAY);
}

esp_err_t sh_hap_set_enabled(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const sh_hap_set_t res = sh_hap_set(s_on, on, s_paired);
    if (res == SH_HAP_SET_RESTART) {
        s_on = on;
        store(KEY_ON, on);
    }
    xSemaphoreGive(s_lock);
    switch (res) {
    case SH_HAP_SET_REFUSED:
        ESP_LOGW(TAG, "Apple HomeKit: off is refused — first remove the device from Apple Home");
        return ESP_ERR_INVALID_STATE;
    case SH_HAP_SET_RESTART:
        ESP_LOGW(TAG, "Apple HomeKit %s: restarting", on ? "on" : "off");
        return sh_reset_later(SH_RESET_RESTART);
    case SH_HAP_SET_SAME:
    default:
        return ESP_OK;
    }
}

void sh_hap_set_lock(bool on, const char *source)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    lock_set(on, source);
    xSemaphoreGive(s_lock);
}

void sh_hap_defaults_reset(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const sh_hap_kept_t kept = { .on = s_on, .paired = s_paired, .lock = s_lock_on, .fahrenheit = s_fahrenheit };
    const unsigned what = sh_hap_defaults(&kept);
    if (what & SH_HAP_DEF_LOCK) {
        lock_set(false, "default settings");
    }
    if (what & SH_HAP_DEF_UNITS) {
        units_set(false, "default settings");
    }
    xSemaphoreGive(s_lock);
}

esp_err_t sh_hap_reset_pairings(void)
{
    if (s_phase != HAP_RUNNING || hap_reset_pairings() != HAP_SUCCESS) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGW(TAG, "HomeKit pairings reset, restarting");    /* the restart is the one of the SDK */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_paired) {
        s_paired = false;
        store(KEY_PAIRED, false);
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t sh_hap_init(void)
{
    s_queue = xQueueCreate(8, sizeof(msg_t));
    s_lock = xSemaphoreCreateMutex();
    if (!s_queue || !s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_on = true;                        /* the default; the factory reset brings it back */
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, KEY_ON, &v) == ESP_OK && v <= 1) {
            s_on = v;
        }
        if (nvs_get_u8(h, KEY_PAIRED, &v) == ESP_OK && v <= 1) {
            s_paired = v;
        }
        if (nvs_get_u8(h, KEY_UNITS, &v) == ESP_OK && v <= 1) {
            s_fahrenheit = v;
        }
        if (nvs_get_u8(h, KEY_LOCK, &v) == ESP_OK && v <= 1) {
            s_lock_on = v;
        }
        nvs_close(h);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {      /* not found: nothing stored yet */
        ESP_LOGE(TAG, "NVS open: %s, defaults used", esp_err_to_name(err));
    }
    s_phase = s_on ? HAP_IDLE : HAP_OFF;
    publish();
    ESP_LOGI(TAG, "Apple HomeKit %s%s, display units °%c, keyboard %s", s_on ? "on" : "off",
             s_on ? (s_paired ? " (paired)" : " (not paired)") : "", s_fahrenheit ? 'F' : 'C',
             s_lock_on ? "locked" : "unlocked");
    /* the stack: hap_init() / hap_start() (mppt-1k starts them in a task of 6144 bytes), with a margin */
    if (xTaskCreate(hap_task, "sh_hap", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "HomeKit task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
