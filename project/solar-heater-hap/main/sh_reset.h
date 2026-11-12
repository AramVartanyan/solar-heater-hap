/* Resets shared by the buttons and the web interface (doc/firmware.md 5 "Нулиране с бутона „меню“";
 * doc/web-interface.md 6).
 *   Wi-Fi     the stored network only; restart into the setup portal
 *   settings  the user settings (menu, frost protection) and the installer data -> defaults, at once
 *   factory   everything in the "nvs" partition (settings, Wi-Fi network, later the HomeKit
 *             pairings); restart */
#pragma once

typedef enum {
    SH_RESET_WIFI = 0,
    SH_RESET_SETTINGS,
    SH_RESET_FACTORY,
} sh_reset_t;

/* Done in the calling task; the Wi-Fi and factory resets do not return. */
void sh_reset_now(sh_reset_t what);

/* Done 0.5 s later in a task of its own (the web server answers first). */
void sh_reset_later(sh_reset_t what);
