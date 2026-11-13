/* Created by Aram Vartanyan (c) 2026 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "mdns.h"
#include "captive_wifi.h"
#include "general_ota.h"
#include "sh_state.h"
#include "sh_clock.h"
#include "sh_web.h"
#include "sh_hap.h"
#include "sh_hap_rules.h"
#include "sh_net.h"

static const char *TAG = "sh_net";

#define TICK_MS         200     /* the rules are stepped this often: every time of theirs within 1 s */

/* Everything the task works on comes through one queue. */
typedef enum {
    MSG_WIFI,                   /* arg: captive_wifi_event_t */
    MSG_MODE,                   /* arg: sh_net_mode_t */
    MSG_OTA_CMD,                /* arg: sh_ota_cmd_t */
    MSG_OTA_EVT,                /* arg: sh_ota_evt_t, pct, version */
    MSG_HAP_FAILED,             /* HAP could not start: the own web server and mDNS instead */
} msg_type_t;

typedef struct {
    uint8_t type;
    uint8_t arg;
    int8_t pct;
    char version[SH_OTA_VERSION_MAX];
} msg_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;        /* s_info */
static sh_net_info_t s_info;
static char s_serial[SH_NET_SERIAL_LEN + 1];

/* Only the network task uses them */
static sh_net_t s_net;
static sh_ota_t s_ota;

static void post(msg_type_t type, int arg, TickType_t wait)
{
    const msg_t m = { .type = (uint8_t)type, .arg = (uint8_t)arg };
    if (s_queue && xQueueSend(s_queue, &m, wait) != pdTRUE) {
        ESP_LOGW(TAG, "queue full: message %d/%d lost", (int)type, arg);
    }
}

/* Runs in the tasks of captive-wifi (and in ours, inside captive_wifi_start() / _stop()). */
static void wifi_event(captive_wifi_event_t evt, void *ctx)
{
    (void)ctx;
    post(MSG_WIFI, (int)evt, 0);
}

/* Runs in the task of general-ota. */
static void ota_event(const general_ota_info_t *info, void *ctx)
{
    (void)ctx;
    msg_t m = { .type = MSG_OTA_EVT, .pct = (int8_t)info->progress_percent };
    switch (info->event) {
    case GENERAL_OTA_EVT_UP_TO_DATE:
        m.arg = SH_OTA_EVT_CURRENT;
        break;
    case GENERAL_OTA_EVT_UPDATE_AVAILABLE:
        m.arg = SH_OTA_EVT_AVAILABLE;
        strlcpy(m.version, info->new_version, sizeof(m.version));
        break;
    case GENERAL_OTA_EVT_PROGRESS:
        m.arg = SH_OTA_EVT_PROGRESS;
        break;
    case GENERAL_OTA_EVT_SUCCESS:
        m.arg = SH_OTA_EVT_SUCCESS;
        break;
    case GENERAL_OTA_EVT_FAILED:
        ESP_LOGW(TAG, "firmware update: %s", esp_err_to_name(info->error));
        m.arg = SH_OTA_EVT_FAILED;
        break;
    default:
        return;                 /* the starts of the check and of the download */
    }
    /* a result must not be lost; a progress step may */
    xQueueSend(s_queue, &m, m.arg == SH_OTA_EVT_PROGRESS ? 0 : pdMS_TO_TICKS(1000));
}

static void do_net(sh_net_act_t act)
{
    switch (act) {
    case SH_NET_ACT_START_AP:
        ESP_LOGI(TAG, "nEt: AP — own access point %s for the web interface", captive_wifi_ap_ssid());
        if (captive_wifi_start_ap(SH_NET_AP_IDLE_S) != ESP_OK) {
            /* CONFIG_CAPTIVE_WIFI_AP_ONLY is off (an old sdkconfig): nothing changed, still CP */
            ESP_LOGE(TAG, "the access point only mode is not built in (CONFIG_CAPTIVE_WIFI_AP_ONLY)");
            s_net.mode = SH_NET_CP;
        }
        break;
    case SH_NET_ACT_START_CP:
        ESP_LOGI(TAG, "nEt: CP — %s", captive_wifi_has_credentials() ? "the router" : "the setup portal");
        sh_web_stop_wait();     /* the portal has its own server on port 80 */
        captive_wifi_start();
        break;
    case SH_NET_ACT_RADIO_OFF:
        ESP_LOGI(TAG, "nEt: CP — no stored network: the radio is off until a restart or a choice of nEt");
        sh_web_stop_wait();
        captive_wifi_stop();
        break;
    case SH_NET_ACT_NONE:
        break;
    }
}

static void do_ota(sh_ota_act_t act, uint32_t now_ms)
{
    esp_err_t err = ESP_OK;
    if (act == SH_OTA_ACT_CHECK) {
        err = general_ota_check();
    } else if (act == SH_OTA_ACT_UPDATE) {
        err = general_ota_update();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "firmware update not started: %s", esp_err_to_name(err));
        sh_ota_event(&s_ota, SH_OTA_EVT_FAILED, NULL, 0, now_ms);
    }
}

/* The web interface after a change of the connection: the own server where it is the one to use
 * (sh_hap_own_web()); once HAP is started, its server has the pages on every interface. */
static void web_update(void)
{
    sh_web_enable(sh_hap_own_web(sh_hap_wanted(), sh_hap_started(), captive_wifi_is_connected(),
                                 captive_wifi_ap_active()));
}

static void on_wifi(captive_wifi_event_t evt, uint32_t now_s)
{
    const bool has_network = captive_wifi_has_credentials();
    sh_net_act_t act = SH_NET_ACT_NONE;
    switch (evt) {
    case CAPTIVE_WIFI_EVT_STA_CONNECTED:
        sh_state_set_wifi(true, false);
        sh_clock_connected();
        /* HomeKit: once, on the first connection to the router; its server takes the pages */
        if (sh_hap_start_due(sh_hap_wanted(), true, sh_hap_started())) {
            sh_hap_start();
        }
        web_update();
        ESP_LOGI(TAG, "connected: http://%s.local/", sh_net_name());
        break;
    case CAPTIVE_WIFI_EVT_STA_DISCONNECTED:
        sh_state_set_wifi(false, captive_wifi_portal_active());
        web_update();
        break;
    case CAPTIVE_WIFI_EVT_PORTAL_STARTED:
        ESP_LOGI(TAG, "Wi-Fi setup: join %s, open http://%s", captive_wifi_ap_ssid(), captive_wifi_portal_ip());
        sh_state_set_wifi(false, true);
        sh_web_enable(false);   /* the portal has its own server on port 80 */
        break;
    case CAPTIVE_WIFI_EVT_PORTAL_STOPPED:
        sh_state_set_wifi(captive_wifi_is_connected(), false);
        break;
    case CAPTIVE_WIFI_EVT_PORTAL_TIMEOUT:
        ESP_LOGI(TAG, "setup portal: no client for %d min, the radio is off until a restart or a choice of nEt",
                 CONFIG_CAPTIVE_WIFI_PORTAL_IDLE_MIN);
        sh_state_set_wifi(false, false);
        act = sh_net_event(&s_net, SH_NET_EVT_PORTAL_TIMEOUT, has_network, now_s);
        break;
    case CAPTIVE_WIFI_EVT_AP_STARTED:
        ESP_LOGI(TAG, "access point: join %s, open http://%s", captive_wifi_ap_ssid(), captive_wifi_portal_ip());
        sh_state_set_wifi(false, false);
        act = sh_net_event(&s_net, SH_NET_EVT_AP_STARTED, has_network, now_s);
        web_update();           /* the web interface on the access point */
        break;
    case CAPTIVE_WIFI_EVT_AP_STOPPED:
        sh_web_enable(false);
        break;
    case CAPTIVE_WIFI_EVT_AP_TIMEOUT:
        act = sh_net_event(&s_net, SH_NET_EVT_AP_TIMEOUT, has_network, now_s);
        break;
    case CAPTIVE_WIFI_EVT_AP_CLIENT_CONNECTED:
        act = sh_net_event(&s_net, SH_NET_EVT_CLIENT_JOINED, has_network, now_s);
        break;
    case CAPTIVE_WIFI_EVT_AP_CLIENT_DISCONNECTED:
        act = sh_net_event(&s_net, SH_NET_EVT_CLIENT_LEFT, has_network, now_s);
        break;
    default:
        break;
    }
    do_net(act);
}

static const char *phase_name(sh_ota_phase_t p)
{
    static const char *const names[] = {
        [SH_OTA_IDLE] = "idle", [SH_OTA_CHECKING] = "checking", [SH_OTA_CURRENT] = "up to date",
        [SH_OTA_AVAILABLE] = "newer version available", [SH_OTA_WAITING] = "confirmed, waiting for the pump",
        [SH_OTA_RUNNING] = "updating", [SH_OTA_DONE] = "written, restarting",
        [SH_OTA_NO_SERVER] = "no connection to the update server", [SH_OTA_FAILED] = "failed, the running version stays",
    };
    return names[p];
}

static void net_task(void *arg)
{
    (void)arg;
    bool published = false;
    bool was_ap = false, was_updating = false, was_offer = false, was_label = false;

    for (;;) {
        msg_t m;
        const bool got = xQueueReceive(s_queue, &m, pdMS_TO_TICKS(TICK_MS)) == pdTRUE;
        const int64_t us = esp_timer_get_time();
        const uint32_t now_ms = (uint32_t)(us / 1000), now_s = (uint32_t)(us / 1000000);

        sh_state_t st;
        sh_state_get(&st);
        const sh_ota_in_t in = {
            .online = s_net.mode == SH_NET_CP && captive_wifi_is_connected(),
            .pump_on = st.pump_on, .alarms = st.alarms, .air = st.modes.air_on,
        };
        const sh_ota_phase_t was_phase = s_ota.phase;
        const bool had_pending = s_net.ap_pending;

        if (got) {
            switch ((msg_type_t)m.type) {
            case MSG_WIFI:
                on_wifi((captive_wifi_event_t)m.arg, now_s);
                break;
            case MSG_MODE:
                if (m.arg == SH_NET_AP && captive_wifi_is_connected()) {
                    ESP_LOGI(TAG, "nEt: AP is refused — connected to the router");
                }
                do_net(sh_net_choose(&s_net, (sh_net_mode_t)m.arg, captive_wifi_is_connected(),
                                     sh_ota_updating(&s_ota), now_s));
                if (s_net.ap_pending && !had_pending) {
                    ESP_LOGI(TAG, "nEt: AP is postponed until the firmware update is over");
                }
                break;
            case MSG_OTA_CMD:
                do_ota(sh_ota_cmd(&s_ota, (sh_ota_cmd_t)m.arg, &in, now_ms), now_ms);
                break;
            case MSG_OTA_EVT:
                sh_ota_event(&s_ota, (sh_ota_evt_t)m.arg, m.version, m.pct, now_ms);
                break;
            case MSG_HAP_FAILED:
                sh_net_mdns_start(true);
                web_update();
                break;
            }
        }
        do_ota(sh_ota_step(&s_ota, &in, now_ms), now_ms);
        do_net(sh_net_step(&s_net, sh_ota_updating(&s_ota), captive_wifi_has_credentials(), now_s));

        if (s_ota.phase != was_phase) {
            ESP_LOGI(TAG, "firmware %s: %s%s%s", general_ota_running_version(), phase_name(s_ota.phase),
                     s_ota.version[0] ? " — " : "", s_ota.version);
        }

        /* for the display, the LED and the web interface: when it changes */
        const bool ap = s_net.mode == SH_NET_AP, updating = sh_ota_updating(&s_ota), label = sh_ota_label(&s_ota);
        if (!published || ap != was_ap || updating != was_updating || s_ota.offer != was_offer || label != was_label) {
            sh_state_set_net(ap, updating, s_ota.offer, label);
            if (s_ota.offer && !was_offer) {
                ESP_LOGI(TAG, "FUp: firmware %s offered on the display for %u s", s_ota.version, SH_OTA_OFFER_MS / 1000u);
            }
            was_ap = ap;
            was_updating = updating;
            was_offer = s_ota.offer;
            was_label = label;
            published = true;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_info.mode = s_net.mode;
        s_info.ap_pending = s_net.ap_pending;
        s_info.ota = s_ota.phase;
        s_info.ota_pct = s_ota.pct;
        memcpy(s_info.ota_version, s_ota.version, sizeof(s_info.ota_version));
        xSemaphoreGive(s_lock);
    }
}

void sh_net_set_mode(sh_net_mode_t mode)
{
    post(MSG_MODE, (int)mode, 0);
}

void sh_net_update(sh_ota_cmd_t cmd)
{
    post(MSG_OTA_CMD, (int)cmd, 0);
}

void sh_net_hap_failed(void)
{
    post(MSG_HAP_FAILED, 0, pdMS_TO_TICKS(1000));
}

esp_err_t sh_net_mdns_start(bool own)
{
    /* mDNS answers on the station only (CONFIG_MDNS_PREDEF_NETIF_AP=n): on the router. With HAP it
     * is hap_start() that starts mDNS (it fails if mDNS is already running); the name — built from
     * the MAC address, not a Kconfig constant — and the web service follow here. */
    esp_err_t err = own ? mdns_init() : ESP_OK;
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;           /* already running (HAP started it and failed later) */
    }
    if (err == ESP_OK) {
        err = mdns_hostname_set(sh_net_name());
    }
    if (err == ESP_OK) {
        err = mdns_service_add("Solar controller", "_http", "_tcp", 80, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS: %s", esp_err_to_name(err));    /* the IP address still works */
    }
    return err;
}

const char *sh_net_access_code(void)
{
    return sh_net_code(captive_wifi_ap_ssid());
}

const char *sh_net_name(void)
{
    return captive_wifi_ap_ssid();
}

const char *sh_net_serial_number(void)
{
    return s_serial;
}

void sh_net_get(sh_net_info_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_lock);
}

esp_err_t sh_net_init(void)
{
    s_queue = xQueueCreate(16, sizeof(msg_t));
    s_lock = xSemaphoreCreateMutex();
    if (!s_queue || !s_lock) {
        return ESP_ERR_NO_MEM;
    }
    /* one name everywhere: the one captive-wifi builds in its init from the base MAC */
    esp_err_t err = captive_wifi_init(NULL, wifi_event, NULL);
    if (err != ESP_OK) {
        return err;
    }
    const char *name = sh_net_name();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    sh_net_serial(s_serial, mac);
    if (!sh_net_name_ok(name, mac)) {
        ESP_LOGE(TAG, "the name %s does not follow the base MAC %s (captive-wifi older than ee1c100?)", name,
                 s_serial);
    }
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta) {
        esp_netif_set_hostname(sta, name);      /* the name at the router (DHCP) */
    }

    /* with Apple HomeKit on, mDNS is started by HAP (on the first connection), not here */
    if (!sh_hap_wanted()) {
        sh_net_mdns_start(true);
    }

    const general_ota_config_t ota = { .cb = ota_event, .auto_reboot = true };
    ESP_LOGI(TAG, "name in the network %s (%s.local), access code %s, serial number %s", name, name,
             sh_net_access_code(), s_serial);
    return general_ota_init(&ota);
}

esp_err_t sh_net_start(void)
{
    if (xTaskCreate(net_task, "sh_net", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "network task not created");
        return ESP_ERR_NO_MEM;
    }
    return captive_wifi_start();
}
