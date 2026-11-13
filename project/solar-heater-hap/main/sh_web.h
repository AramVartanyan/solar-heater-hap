/* Web interface (doc/web-interface.md; doc/tasks/step6-installer-web.md 3.4): one page embedded
 * in the firmware and a small JSON API, on an own esp_http_server (port 80). The server runs while
 * the device is connected to the router (CP) or its own access point is on (AP, http://192.168.4.1),
 * and never together with the Wi-Fi setup portal — the portal has its own server on the same port.
 * With HomeKit on (group F) the handlers are registered on the server of esp-homekit-sdk once HAP
 * has started (sh_web_attach()); before that the own server runs only on the own access point.
 *
 *   GET  /            the page
 *   GET  /api/state   status (sh_web_api: JSON)
 *   POST /api/set     settings; the installer data only with the access code
 *   POST /api/test    pump test: start, +60 s, -60 s, stop (as the buttons)
 *   POST /api/air     air venting: start, stop
 *   POST /api/reset   Wi-Fi, default settings, factory, history, learned values — with the code
 *   POST /api/auth    checks the access code (the page asks once and keeps it)
 *   GET  /api/chart   the chart of the day (sh_history, RAM)
 *   GET  /api/days    the last 7 daily summaries
 *   GET  /api/history.csv  all daily summaries as CSV
 *   GET  /api/tz      the supported time zones
 *   POST /api/time    the time of the browser (no code; taken only without SNTP in the last 24 h)
 *   POST /api/tz      the time zone, by its index — with the access code
 *   POST /api/ota     firmware update: check, update — with the access code
 *   POST /api/hk      HomeKit: the setting "Apple HomeKit" (then a restart), the keypad lock — with
 *                     the access code
 * Answers of the POSTs: 200, 400 (bad request, nothing changed), 403 (access code).
 * The access code: the last 6 characters of the setup network name ("Solar-810840" -> 810840; capitals are taken too). */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t sh_web_init(void);        /* after sh_net_init() (captive_wifi_init()) */

/* Start / stop the server; from the Wi-Fi events (any task: the work is done by the web task). */
void sh_web_enable(bool on);

/* Stop the server and wait until it is down (before the setup portal may start its own). */
void sh_web_stop_wait(void);

/* HAP has started (sh_hap): the pages on its server from now on; the own server never runs again. */
void sh_web_attach(httpd_handle_t server);
