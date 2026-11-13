#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "nvs.h"
#include "mdns.h"
#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>
#include <hap_platform_httpd.h>
#include "sdkconfig.h"
#include "sh_state.h"
#include "sh_settings_store.h"
#include "sh_led.h"
#include "sh_net.h"
#include "sh_web.h"
#include "sh_hap_rules.h"
#include "sh_hap.h"

static const char *TAG = "sh_hap";

#define NVS_NAMESPACE   "sh_hap"
#define KEY_ON          "on"
#define PUSH_MS         1000        /* the changes are checked this often; only changes are sent */

static int8_t s_enabled = -1;       /* -1: not read yet */
static bool s_init;                 /* hap_init() and the accessory done */
static volatile bool s_started;
static bool s_starting;
static bool s_window;               /* the pairing window (sh_hap_window) */

static hap_char_t *c_active, *c_state, *c_target, *c_temp, *c_threshold, *c_units, *c_lock;
static hap_char_t *c_col_temp, *c_col_fault, *c_tank_temp, *c_tank_fault;

/* The last view sent (only the push task and the write callback touch it) */
static sh_hap_view_t s_view;
static portMUX_TYPE s_view_lock = portMUX_INITIALIZER_UNLOCKED;

static void current(sh_hap_view_t *out)
{
    sh_state_t st;
    sh_state_get_shown(&st);         /* pulse mode: the last reliable values */
    sh_settings_t set;
    sh_settings_get(&set);
    const sh_hap_in_t in = {
        .vacation = set.v[SH_SET_VACATION] != 0, .pump_on = st.pump_on,
        .cooling = st.pump_on && st.pump_reason == SH_PUMP_REASON_COOL,
        .t_tank = st.t_tank, .t_col = st.t_col, .tank_fault = st.tank_fault, .col_fault = st.col_fault,
        .alarms = st.alarms, .tmax = set.v[SH_SET_TANK_MAX],
        .locked = sh_settings_locked(), .fahrenheit = sh_settings_fahrenheit(),
    };
    taskENTER_CRITICAL(&s_view_lock);
    const sh_hap_view_t prev = s_view;
    taskEXIT_CRITICAL(&s_view_lock);
    sh_hap_view(&in, s_init ? &prev : NULL, out);
}

static void set_u8(hap_char_t *hc, uint8_t v)
{
    hap_val_t val = { .u = v };
    hap_char_update_val(hc, &val);
}

static void set_temp(hap_char_t *hc, int16_t t10)
{
    hap_val_t val = { .f = t10 / 10.0f };
    hap_char_update_val(hc, &val);
}

/* ---- values read on request only (never notified) ----------------------------------------- */

static int read_cb(hap_char_t *hc, hap_status_t *status, void *serv_priv, void *read_priv)
{
    (void)serv_priv;
    (void)read_priv;
    if (hc == c_temp || hc == c_tank_temp || hc == c_col_temp) {
        sh_hap_view_t v;
        current(&v);
        taskENTER_CRITICAL(&s_view_lock);
        s_view.tank = v.tank;           /* the kept values while a sensor is faulty */
        s_view.col = v.col;
        taskEXIT_CRITICAL(&s_view_lock);
        set_temp(hc, hc == c_col_temp ? v.col : v.tank);
    }
    *status = HAP_STATUS_SUCCESS;
    return HAP_SUCCESS;
}

/* ---- writes of the controllers ------------------------------------------------------------- */

static int write_cb(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv)
{
    (void)serv_priv;
    (void)write_priv;
    int ret = HAP_SUCCESS;
    for (int i = 0; i < count; i++) {
        hap_write_data_t *w = &write_data[i];
        bool ok = false, flag = false;
        int16_t tmax = 0;
        if (w->hc == c_active && sh_hap_write_active(w->val.u, &flag)) {
            ESP_LOGI(TAG, "Active %u from HomeKit: vacation %s", (unsigned)w->val.u, flag ? "on" : "off");
            ok = sh_settings_set(SH_SET_VACATION, flag) == ESP_OK;
        } else if (w->hc == c_target) {
            ok = sh_hap_write_target(w->val.u);         /* only Heat */
        } else if (w->hc == c_threshold && sh_hap_write_threshold(w->val.f, &tmax)) {
            ESP_LOGI(TAG, "Heating Threshold from HomeKit: Tmax %d degC", tmax);
            ok = sh_settings_set(SH_SET_TANK_MAX, tmax) == ESP_OK;
            w->val.f = tmax;
        } else if (w->hc == c_units && sh_hap_write_flag(w->val.u, &flag)) {
            ESP_LOGI(TAG, "display units from HomeKit: %s", flag ? "degF" : "degC");
            ok = sh_settings_set_fahrenheit(flag) == ESP_OK;
        } else if (w->hc == c_lock && sh_hap_write_flag(w->val.u, &flag)) {
            ESP_LOGI(TAG, "keypad %s from HomeKit", flag ? "locked" : "unlocked");
            ok = sh_settings_set_locked(flag) == ESP_OK;
        }
        if (!ok) {
            *(w->status) = HAP_STATUS_VAL_INVALID;
            ret = HAP_FAIL;
            continue;
        }
        hap_char_update_val(w->hc, &w->val);
        *(w->status) = HAP_STATUS_SUCCESS;
        /* known to the controllers now: not notified again by the push */
        taskENTER_CRITICAL(&s_view_lock);
        if (w->hc == c_active) {
            s_view.active = w->val.u;
        } else if (w->hc == c_threshold) {
            s_view.threshold = (uint8_t)tmax;
        } else if (w->hc == c_units) {
            s_view.units = w->val.u;
        } else if (w->hc == c_lock) {
            s_view.lock = w->val.u;
        }
        taskEXIT_CRITICAL(&s_view_lock);
    }
    return ret;
}

static int identify(hap_acc_t *ha)
{
    (void)ha;
    ESP_LOGI(TAG, "identify");
    sh_led_identify();
    return HAP_SUCCESS;
}

/* ---- events of the SDK: the pairing window ------------------------------------------------- */

static void window(sh_hap_w_evt_t evt)
{
    const bool open = sh_hap_window(s_window, evt, hap_get_paired_controller_count());
    if (open != s_window) {
        ESP_LOGI(TAG, "pairing window %s", open ? "open (10 min)" : "closed");
    }
    s_window = open;
    sh_state_set_pair_window(open);
}

static void hap_event(hap_event_t event, void *data)
{
    (void)data;
    switch (event) {
    case HAP_EVENT_CTRL_PAIRED:
        ESP_LOGI(TAG, "paired with a controller");
        window(SH_HAP_W_PAIRED);
        break;
    case HAP_EVENT_CTRL_UNPAIRED:
        ESP_LOGI(TAG, "a controller removed, %d left", hap_get_paired_controller_count());
        window(SH_HAP_W_UNPAIRED);
        break;
    case HAP_EVENT_PAIRING_MODE_TIMED_OUT:
        ESP_LOGI(TAG, "pairing window timed out (HAP keeps running)");
        window(SH_HAP_W_TIMEOUT);
        break;
    case HAP_EVENT_PAIRING_STARTED:
        ESP_LOGI(TAG, "pairing started");
        break;
    case HAP_EVENT_PAIRING_ABORTED:
        ESP_LOGI(TAG, "pairing aborted");
        break;
    default:
        break;
    }
}

/* ---- the accessory ------------------------------------------------------------------------- */

static hap_serv_t *sensor(const char *name, int16_t t10, int min, int max, hap_char_t **temp, hap_char_t **fault)
{
    hap_serv_t *s = hap_serv_temperature_sensor_create(t10 / 10.0f);
    hap_serv_add_char(s, hap_char_name_create((char *)name));
    *temp = hap_serv_get_char_by_uuid(s, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    hap_char_float_set_constraints(*temp, (float)min, (float)max, 0.1f);
    *fault = hap_char_status_fault_create(0);
    hap_serv_add_char(s, *fault);
    hap_serv_set_read_cb(s, read_cb);
    return s;
}

static esp_err_t accessory(void)
{
    if (hap_init(HAP_TRANSPORT_WIFI) != HAP_SUCCESS) {
        return ESP_FAIL;
    }
    current(&s_view);
    const sh_hap_view_t *v = &s_view;
    static char name[24], serial[16], version[32];
    strlcpy(name, sh_net_name(), sizeof(name));
    strlcpy(serial, sh_net_serial_number(), sizeof(serial));
    strlcpy(version, esp_app_get_description()->version, sizeof(version));
    hap_acc_cfg_t cfg = {
        .name = name,
        .manufacturer = "Armo Ltd.",
        .model = "Solar Heater",
        .serial_num = serial,
        .fw_rev = version,
        .pv = "1.1.0",
        .identify_routine = identify,
        .cid = HAP_CID_HEATER,
    };
    hap_acc_t *acc = hap_acc_create(&cfg);
    if (!acc) {
        return ESP_ERR_NO_MEM;
    }
    hap_acc_add_wifi_transport_service(acc, 0);

    /* Heater Cooler (primary): Active, Current Temperature, Current / Target State; then Heating
     * Threshold, Name, Temperature Display Units, Lock Physical Controls */
    hap_serv_t *hc = hap_serv_heater_cooler_create(v->active, v->tank / 10.0f, v->state, SH_HAP_TARGET_HEAT);
    c_active = hap_serv_get_char_by_uuid(hc, HAP_CHAR_UUID_ACTIVE);
    c_temp = hap_serv_get_char_by_uuid(hc, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    c_state = hap_serv_get_char_by_uuid(hc, HAP_CHAR_UUID_CURRENT_HEATER_COOLER_STATE);
    c_target = hap_serv_get_char_by_uuid(hc, HAP_CHAR_UUID_TARGET_HEATER_COOLER_STATE);
    static const uint8_t heat_only[] = { SH_HAP_TARGET_HEAT };
    hap_char_add_valid_vals(c_target, heat_only, sizeof(heat_only));
    hap_char_float_set_constraints(c_temp, SH_HAP_TANK_MIN, SH_HAP_TANK_MAX, 0.1f);
    c_threshold = hap_char_heating_threshold_temperature_create(v->threshold);
    hap_char_float_set_constraints(c_threshold, SH_HAP_TMAX_MIN, SH_HAP_TMAX_MAX, 1.0f);
    hap_serv_add_char(hc, c_threshold);
    hap_serv_add_char(hc, hap_char_name_create(name));
    c_units = hap_char_temperature_display_units_create(v->units);
    hap_serv_add_char(hc, c_units);
    c_lock = hap_char_lock_physical_controls_create(v->lock);
    hap_serv_add_char(hc, c_lock);
    hap_serv_set_read_cb(hc, read_cb);
    hap_serv_set_write_cb(hc, write_cb);
    hap_serv_mark_primary(hc);
    hap_acc_add_serv(acc, hc);

    hap_acc_add_serv(acc, sensor("Collector", v->col, SH_HAP_COL_MIN, SH_HAP_COL_MAX, &c_col_temp, &c_col_fault));
    hap_acc_add_serv(acc, sensor("Tank", v->tank, SH_HAP_TANK_MIN, SH_HAP_TANK_MAX, &c_tank_temp, &c_tank_fault));
    set_u8(c_col_fault, v->col_fault);
    set_u8(c_tank_fault, v->tank_fault);
    hap_add_accessory(acc);

#ifdef CONFIG_SH_HAP_FIXED_SETUP_CODE
    /* development only; a product reads the setup info from factory_nvs */
    hap_set_setup_code(CONFIG_SH_HAP_SETUP_CODE);
    hap_set_setup_id(CONFIG_SH_HAP_SETUP_ID);
    ESP_LOGW(TAG, "fixed setup code %s (development only), payload %s", CONFIG_SH_HAP_SETUP_CODE,
             esp_hap_get_setup_payload(CONFIG_SH_HAP_SETUP_CODE, CONFIG_SH_HAP_SETUP_ID, false, cfg.cid));
#endif
    hap_register_event_handler(hap_event);
    return ESP_OK;
}

/* ---- start and the changes ----------------------------------------------------------------- */

static void push(void)
{
    sh_hap_view_t now;
    current(&now);
    taskENTER_CRITICAL(&s_view_lock);
    const sh_hap_view_t prev = s_view;
    s_view = now;
    taskEXIT_CRITICAL(&s_view_lock);
    const unsigned n = sh_hap_notify(&prev, &now);
    if (n & SH_HAP_N_ACTIVE) {
        set_u8(c_active, now.active);
    }
    if (n & SH_HAP_N_STATE) {
        set_u8(c_state, now.state);
    }
    if (n & SH_HAP_N_THRESHOLD) {
        hap_val_t val = { .f = now.threshold };
        hap_char_update_val(c_threshold, &val);
    }
    if (n & SH_HAP_N_UNITS) {
        set_u8(c_units, now.units);
    }
    if (n & SH_HAP_N_LOCK) {
        set_u8(c_lock, now.lock);
    }
    if (n & SH_HAP_N_COL_FAULT) {
        set_u8(c_col_fault, now.col_fault);
    }
    if (n & SH_HAP_N_TANK_FAULT) {
        set_u8(c_tank_fault, now.tank_fault);
    }
}

static void hap_task(void *arg)
{
    (void)arg;
    if (hap_start() != HAP_SUCCESS) {
        ESP_LOGE(TAG, "hap_start failed: HomeKit does not run until a restart");
        s_starting = false;
        vTaskDelete(NULL);
        return;
    }
    s_started = true;
    /* mDNS is HAP's now (hap_mdns_init()): the name and the web service after it */
    const char *name = sh_net_name();
    esp_err_t err = mdns_hostname_set(name);
    if (err == ESP_OK) {
        err = mdns_service_add("Solar controller", "_http", "_tcp", 80, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS: %s", esp_err_to_name(err));    /* the IP address still works */
    }
    httpd_handle_t *server = hap_platform_httpd_get_handle();
    if (server && *server) {
        sh_web_attach(*server);
    } else {
        ESP_LOGE(TAG, "no HAP HTTP server: no web interface");
    }
    window(SH_HAP_W_STARTED);
    ESP_LOGI(TAG, "HomeKit started as %s (%d paired controllers), http://%s.local/", name,
             hap_get_paired_controller_count(), name);
    for (;;) {
        push();
        vTaskDelay(pdMS_TO_TICKS(PUSH_MS));
    }
}

bool sh_hap_enabled(void)
{
    if (s_enabled < 0) {
        uint8_t on = 1;                 /* on by default */
        nvs_handle_t h;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
            nvs_get_u8(h, KEY_ON, &on);
            nvs_close(h);
        }
        s_enabled = on != 0;
    }
    return s_enabled;
}

esp_err_t sh_hap_store_enabled(bool on)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, KEY_ON, on);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "Apple HomeKit %s: stored, restarting", on ? "on" : "off");
    } else {
        ESP_LOGE(TAG, "Apple HomeKit %s: %s", on ? "on" : "off", esp_err_to_name(err));
    }
    return err;
}

bool sh_hap_started(void)
{
    return s_started;
}

bool sh_hap_paired(void)
{
    return s_init && hap_get_paired_controller_count() > 0;
}

void sh_hap_connected(void)
{
    if (!s_init || s_starting || !sh_hap_should_start(true, s_started, true)) {
        return;
    }
    s_starting = true;
    if (xTaskCreate(hap_task, "sh_hap", 6144, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "HomeKit task not created");
        s_starting = false;
    }
}

esp_err_t sh_hap_reset_pairings(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGW(TAG, "erasing the HomeKit pairings, restarting");
    return hap_reset_pairings() == HAP_SUCCESS ? ESP_OK : ESP_FAIL;
}

bool sh_hap_reset_to_factory(void)
{
    return s_started && hap_reset_to_factory() == HAP_SUCCESS;
}

esp_err_t sh_hap_init(void)
{
    if (!sh_hap_enabled()) {
        ESP_LOGI(TAG, "Apple HomeKit off (the setting)");
        return ESP_OK;
    }
    const esp_err_t err = accessory();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HomeKit accessory not created: %s", esp_err_to_name(err));
        return err;
    }
    s_init = true;
    ESP_LOGI(TAG, "Apple HomeKit on: %s, %d paired controllers; starts with the connection to the router",
             sh_net_name(), hap_get_paired_controller_count());
    return ESP_OK;
}
