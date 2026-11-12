/* Web interface server (doc/web-interface.md; doc/tasks/step6-installer-web.md 3.4): its own
 * esp_http_server on port 80, only while the device is connected to the router and the Wi-Fi setup
 * portal is not active (the portal's server uses port 80 too). One page embedded in the firmware
 * (web/index.html) and the requests of sh_web_data.h. With HomeKit the page and the requests move
 * to the server of esp-homekit-sdk (a later step). */
#pragma once

#include "esp_err.h"

esp_err_t sh_web_start(void);   /* no-op if running */
void sh_web_stop(void);         /* no-op if stopped */
