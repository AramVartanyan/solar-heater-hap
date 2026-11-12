/* Web interface (doc/web-interface.md; doc/tasks/step6-installer-web.md 3.4): one page embedded
 * in the firmware and a small JSON API, on an own esp_http_server (port 80). The server runs only
 * while the device is connected to the router and the Wi-Fi setup portal is closed — the portal
 * has its own server on the same port. With HomeKit the handlers move to the server of
 * esp-homekit-sdk.
 *
 *   GET  /            the page
 *   GET  /api/state   status (sh_web_api: JSON)
 *   POST /api/set     settings; the installer data only with the access code
 *   POST /api/test    pump test: start, +60 s, -60 s, stop (as the buttons)
 *   POST /api/reset   Wi-Fi, default settings, factory — with the access code
 *   POST /api/auth    checks the access code (the page asks once and keeps it)
 * Answers of the POSTs: 200, 400 (bad request, nothing changed), 403 (access code).
 * The access code: the last 6 characters of the setup network name ("SOLAR-810841" -> 810841). */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t sh_web_init(void);        /* after captive_wifi_init() */

/* Start / stop the server; from the Wi-Fi events (any task: the work is done by the web task). */
void sh_web_enable(bool on);
