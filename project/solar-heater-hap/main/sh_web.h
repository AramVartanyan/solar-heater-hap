/* Created by Aram Vartanyan (c) 2026 */

/* Web interface (doc/web-interface.md): one page embedded
 * in the firmware and a small JSON API, on an own esp_http_server (port 80). The server runs while
 * the device is connected to the router (CP) or its own access point is on (AP, http://192.168.4.1),
 * and never together with the Wi-Fi setup portal — the portal has its own server on the same port.
 * With Apple HomeKit on the same handlers are registered on the HTTP server of
 * esp-homekit-sdk once HAP is started (sh_web_attach()); the own server is then not used any more.
 *
 *   GET  /            the page
 *   GET  /api/state   status (sh_web_api: JSON)
 *   POST /api/set     settings; the installer data only with the access code
 *   POST /api/test    pump test: start, +60 s, -60 s, stop (as the buttons)
 *   POST /api/air     air venting: start, stop
 *   POST /api/reset   Wi-Fi, HomeKit pairings, default settings, factory, history, learned values —
 *                     with the code
 *   POST /api/hap     the setting "Apple HomeKit": on=0 | 1 — with the code; stored, then a restart
 *   POST /api/lock    the keyboard lock: on=0 | 1 — with the code
 *   POST /api/auth    checks the access code (the page asks once and keeps it)
 *   GET  /api/chart   the chart of the day (sh_history, RAM)
 *   GET  /api/days    the last 7 daily summaries
 *   GET  /api/history.csv  all daily summaries as CSV
 *   GET  /api/tz      the supported time zones
 *   POST /api/time    the time of the browser (no code; taken only without SNTP in the last 24 h)
 *   POST /api/tz      the time zone, by its index — with the access code
 *   POST /api/ota     firmware update: check, update — with the access code
 * Answers of the POSTs: 200, 400 (bad request, nothing changed), 403 (access code).
 * The access code: the last 6 characters of the setup network name ("Solar-810840" -> 810840; capitals are taken too). */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t sh_web_init(void);        /* after sh_net_init() (captive_wifi_init()) */

/* Start / stop the own server; from the Wi-Fi events (any task: the work is done by the web task).
 * Ignored once the pages are on the server of HAP. */
void sh_web_enable(bool on);

/* The pages on the HTTP server of HAP (after hap_start(); the own server must be down:
 * sh_web_stop_wait()). */
void sh_web_attach(httpd_handle_t hap_server);

/* Stop the server and wait until it is down (before the setup portal may start its own). */
void sh_web_stop_wait(void);
