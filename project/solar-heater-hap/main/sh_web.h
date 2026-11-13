/* Web interface (doc/web-interface.md; doc/tasks/step6-installer-web.md 3.4): one page embedded
 * in the firmware and a small JSON API, on an own esp_http_server (port 80). The server runs only
 * while the device is connected to the router or its own access point (AP) is on, and the Wi-Fi
 * setup portal is closed — the portal
 * has its own server on the same port. With HomeKit the handlers move to the server of
 * esp-homekit-sdk.
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
 *   POST /api/ota     the firmware update: check / update — with the access code, not in AP
 * Answers of the POSTs: 200, 400 (bad request, nothing changed), 403 (access code).
 * The server runs also on the own access point (network mode AP, http://192.168.4.1).
 * The access code: the last 6 characters of the network name ("Solar-810840" -> 810840). */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t sh_web_init(void);        /* after captive_wifi_init() */

/* Start / stop the server; from the Wi-Fi events (any task: the work is done by the web task). */
void sh_web_enable(bool on);
