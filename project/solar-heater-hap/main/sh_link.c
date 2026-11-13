#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "mdns.h"
#include "captive_wifi.h"
#include "general_ota.h"
#include "sdkconfig.h"
#include "sh_state.h"
#include "sh_clock.h"
#include "sh_web.h"
#include "sh_link.h"

static const char *TAG = "sh_link";

#define TICK_MS     250

/* What the task gets: the Wi-Fi and OTA events, the requests. */
typedef enum {
    MSG_WIFI,                   /* arg: captive_wifi_event_t */
    MSG_OTA,                    /* arg: general_ota_event_t, with ver / pct / err */
    MSG_SELECT,                 /* arg: sh_net_mode_t */
    MSG_FW_CHECK,
    MSG_FW_UPDATE,
    MSG_FW_CONFIRM,
} msg_kind_t;

typedef struct {
    msg_kind_t kind;
    int arg;
    int pct;
    char ver[SH_FW_VER_LEN];
} msg_t;

static QueueHandle_t s_queue;
static char s_name[SH_NET_NAME_MAX];

/* Only the task changes these; sh_link_get() copies the info under the lock. */
static sh_net_t s_net;
static sh_fw_t s_fw;
static sh_link_info_t s_info;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void post(const msg_t *m)
{
    if (s_queue) {
        xQueueSend(s_queue, m, 0);
    }
}

/* captive-wifi task (or this task, inside a captive_wifi_* call): only queued */
static void wifi_cb(captive_wifi_event_t evt, void *ctx)
{
    (void)ctx;
    const msg_t m = { .kind = MSG_WIFI, .arg = evt };
    post(&m);
}

/* general-ota task: only queued */
static void ota_cb(const general_ota_info_t *i, void *ctx)
{
    (void)ctx;
    msg_t m = { .kind = MSG_OTA, .arg = i->event, .pct = i->progress_percent };
    snprintf(m.ver, sizeof(m.ver), "%s", i->new_version ? i->new_version : "");
    if (i->event == GENERAL_OTA_EVT_FAILED) {
        ESP_LOGW(TAG, "firmware update: %s", esp_err_to_name(i->error));
    }
    post(&m);
}

static void apply(sh_net_act_t act)
{
    esp_err_t err = ESP_OK;
    switch (act) {
    case SH_NET_ACT_START_AP:
        ESP_LOGI(TAG, "network mode AP: join %s, open http://%s", captive_wifi_ap_ssid(),
                 captive_wifi_portal_ip());
        err = captive_wifi_start_ap(SH_NET_AP_IDLE_S);
        break;
    case SH_NET_ACT_START_CP:
        ESP_LOGI(TAG, "network mode CP");
        err = captive_wifi_start();
        break;
    case SH_NET_ACT_RADIO_OFF:
        ESP_LOGI(TAG, "network mode CP: no network stored, radio off");
        err = captive_wifi_stop();
        break;
    case SH_NET_ACT_NONE:
        break;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network mode: %s", esp_err_to_name(err));
    }
}

static void on_wifi(captive_wifi_event_t evt, uint32_t now)
{
    switch (evt) {
    case CAPTIVE_WIFI_EVT_STA_CONNECTED:
        sh_state_set_wifi(true, false);
        sh_clock_connected();
        sh_web_enable(true);
        sh_fw_link(&s_fw, true, now);
        break;
    case CAPTIVE_WIFI_EVT_STA_DISCONNECTED:
        sh_state_set_wifi(false, captive_wifi_portal_active());
        sh_fw_link(&s_fw, false, now);
        if (!captive_wifi_ap_active()) {
            sh_web_enable(false);
        }
        break;
    case CAPTIVE_WIFI_EVT_PORTAL_STARTED:
        ESP_LOGI(TAG, "Wi-Fi setup: join %s, open http://%s", captive_wifi_ap_ssid(), captive_wifi_portal_ip());
        sh_state_set_wifi(false, true);
        sh_web_enable(false);       /* the portal has its own server on port 80 */
        break;
    case CAPTIVE_WIFI_EVT_PORTAL_STOPPED:
        sh_state_set_wifi(captive_wifi_is_connected(), false);
        break;
    case CAPTIVE_WIFI_EVT_PORTAL_TIMEOUT:
        ESP_LOGI(TAG, "Wi-Fi setup: no client, radio off");
        sh_net_portal_timeout(&s_net);
        sh_state_set_wifi(false, false);
        break;
    case CAPTIVE_WIFI_EVT_AP_STARTED:
        sh_state_set_wifi(false, false);
        sh_web_enable(true);        /* the web interface on the access point */
        break;
    case CAPTIVE_WIFI_EVT_AP_STOPPED:
        sh_web_enable(false);
        break;
    case CAPTIVE_WIFI_EVT_AP_TIMEOUT: {
        const sh_net_act_t act = sh_net_ap_timeout(&s_net, captive_wifi_has_credentials());
        if (act != SH_NET_ACT_RADIO_OFF) {
            apply(act);             /* the library switched the radio off already */
        } else {
            ESP_LOGI(TAG, "network mode CP: no network stored, radio off");
        }
        break;
    }
    case CAPTIVE_WIFI_EVT_AP_CLIENT_CONNECTED:
    case CAPTIVE_WIFI_EVT_AP_CLIENT_DISCONNECTED:
        sh_net_client(&s_net, evt == CAPTIVE_WIFI_EVT_AP_CLIENT_CONNECTED, now);
        break;
    default:
        break;
    }
}

static void on_ota(const msg_t *m)
{
    switch ((general_ota_event_t)m->arg) {
    case GENERAL_OTA_EVT_UP_TO_DATE:
        if (s_fw.state == SH_FW_UPDATING) {
            sh_fw_update_done(&s_fw, false);
        }
        sh_fw_check_done(&s_fw, SH_FW_RES_CURRENT, m->ver, now_s());
        ESP_LOGI(TAG, "firmware %s is up to date", general_ota_running_version());
        break;
    case GENERAL_OTA_EVT_UPDATE_AVAILABLE:
        if (s_fw.state == SH_FW_CHECKING) {
            ESP_LOGI(TAG, "firmware %s is available (running %s)", m->ver, general_ota_running_version());
        }
        sh_fw_check_done(&s_fw, SH_FW_RES_AVAILABLE, m->ver, now_s());
        break;
    case GENERAL_OTA_EVT_PROGRESS:
        sh_fw_progress(&s_fw, m->pct);
        break;
    case GENERAL_OTA_EVT_SUCCESS:
        ESP_LOGI(TAG, "firmware %s written, restarting", m->ver);
        sh_fw_update_done(&s_fw, true);     /* general-ota restarts (auto_reboot) */
        break;
    case GENERAL_OTA_EVT_FAILED:
        if (s_fw.state == SH_FW_UPDATING) {
            ESP_LOGW(TAG, "firmware update failed: the old version stays");
            sh_fw_update_done(&s_fw, false);
        } else {
            sh_fw_check_done(&s_fw, SH_FW_RES_ERROR, NULL, now_s());   /* the log only */
        }
        break;
    default:
        break;
    }
}

static void start_check(bool automatic)
{
    if (!sh_fw_check_start(&s_fw, automatic)) {
        return;
    }
    ESP_LOGI(TAG, "firmware: %s check", automatic ? "automatic" : "manual");
    if (general_ota_check() != ESP_OK) {
        sh_fw_check_done(&s_fw, SH_FW_RES_ERROR, NULL, now_s());
    }
}

static void on_msg(const msg_t *m, uint32_t now)
{
    switch (m->kind) {
    case MSG_WIFI:
        on_wifi((captive_wifi_event_t)m->arg, now);
        break;
    case MSG_OTA:
        on_ota(m);
        break;
    case MSG_SELECT: {
        const bool connected = captive_wifi_is_connected();
        if (m->arg == SH_NET_AP && !sh_net_ap_allowed(connected)) {
            ESP_LOGI(TAG, "network mode AP refused: connected to the router");
        }
        apply(sh_net_select(&s_net, (sh_net_mode_t)m->arg, connected, sh_fw_busy(&s_fw), now));
        if (s_net.ap_pending) {
            ESP_LOGI(TAG, "network mode AP: after the firmware update");
        }
        break;
    }
    case MSG_FW_CHECK:
        if (s_net.mode == SH_NET_CP) {
            start_check(false);
        }
        break;
    case MSG_FW_UPDATE:
    case MSG_FW_CONFIRM:
        if (sh_fw_confirm(&s_fw, now, m->kind == MSG_FW_CONFIRM)) {
            ESP_LOGI(TAG, "firmware %s: confirmed, waiting for the pump to stop", s_fw.ver);
        }
        break;
    }
}

static void tick(uint32_t now)
{
    apply(sh_net_tick(&s_net, now, captive_wifi_has_credentials()));
    if (s_net.mode == SH_NET_CP && sh_fw_auto_due(&s_fw, now)) {
        start_check(true);
    }
    sh_state_t st;
    sh_state_get(&st);
    if (sh_fw_step(&s_fw, st.pump_on, st.alarms)) {
        ESP_LOGI(TAG, "firmware %s: updating", s_fw.ver);
        if (general_ota_update() != ESP_OK) {
            sh_fw_update_done(&s_fw, false);
        }
    }
    if (!sh_fw_busy(&s_fw)) {
        apply(sh_net_fw_done(&s_net, now));
    }
    sh_state_set_link(s_net.mode == SH_NET_AP, sh_fw_prompt(&s_fw, now), s_fw.state == SH_FW_WAITING,
                      s_fw.state == SH_FW_UPDATING);

    sh_link_info_t info = {
        .mode = s_net.mode, .ap_pending = s_net.ap_pending, .clients = s_net.clients,
        .ap_left_s = sh_net_ap_left(&s_net, now), .fw = s_fw.state, .fw_pct = s_fw.pct,
    };
    memcpy(info.fw_ver, s_fw.ver, sizeof(info.fw_ver));
    taskENTER_CRITICAL(&s_lock);
    s_info = info;
    taskEXIT_CRITICAL(&s_lock);
}

static void link_task(void *arg)
{
    (void)arg;
    for (;;) {
        msg_t m;
        if (xQueueReceive(s_queue, &m, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            on_msg(&m, now_s());
        }
        tick(now_s());
    }
}

esp_err_t sh_link_start(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    sh_net_name(s_name, sizeof(s_name), CONFIG_CAPTIVE_WIFI_AP_SSID_PREFIX, mac);
    sh_net_init(&s_net);
    s_queue = xQueueCreate(16, sizeof(msg_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = captive_wifi_init(s_name, wifi_cb, NULL);     /* DHCP host name */
    if (err != ESP_OK) {
        return err;
    }
    if (strcmp(s_name, captive_wifi_ap_ssid()) != 0) {
        ESP_LOGW(TAG, "name %s differs from the network %s", s_name, captive_wifi_ap_ssid());
    }

    /* mDNS: "<name>.local" and the web service, on the station only (CP with the router) */
    err = mdns_init();
    if (err == ESP_OK) {
        mdns_hostname_set(s_name);
        mdns_instance_name_set(s_name);
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    } else {
        ESP_LOGE(TAG, "mdns_init: %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(sh_clock_start());
    ESP_ERROR_CHECK(sh_web_init());
    const general_ota_config_t ota = { .cb = ota_cb, .auto_reboot = true };
    ESP_ERROR_CHECK(general_ota_init(&ota));

    if (xTaskCreate(link_task, "sh_link", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "name %s (%s.local)", s_name, s_name);
    return captive_wifi_start();                    /* always CP at the start */
}

const char *sh_link_hostname(void)
{
    return s_name;
}

void sh_link_select(sh_net_mode_t mode)
{
    const msg_t m = { .kind = MSG_SELECT, .arg = mode };
    post(&m);
}

void sh_link_fw_check(void)
{
    const msg_t m = { .kind = MSG_FW_CHECK };
    post(&m);
}

void sh_link_fw_update(void)
{
    const msg_t m = { .kind = MSG_FW_UPDATE };
    post(&m);
}

void sh_link_fw_confirm(void)
{
    const msg_t m = { .kind = MSG_FW_CONFIRM };
    post(&m);
}

void sh_link_get(sh_link_info_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_info;
    taskEXIT_CRITICAL(&s_lock);
}
