/* The clock (doc/operating-modes.md 3.1; doc/tasks/step9-clock-history.md 3.1): the system time in
 * UTC, set by SNTP (pool.ntp.org: at every connection to the router and every 12 h after it) or by
 * the browser (only if SNTP did not set it during the last 24 h), and the time zone — confirmed in
 * the web interface, kept by its name in the "nvs" partition (namespace "sh_clock", key "tz");
 * without it the local time is UTC. After boot the clock is not set until the first of the two.
 * The rules and the zone list: sh_clock. Whether the clock is reliable (the time zone check by the
 * collector peak) is kept with the history: sh_history_store, published in sh_state. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sh_clock.h"

typedef struct {
    bool set;                   /* set since boot */
    uint16_t day;               /* local date, days since 2020-01-01 (sh_clock_date()) */
    uint32_t sec;               /* local time, seconds since midnight */
    int16_t zone;               /* index in sh_zones; SH_ZONE_NONE: not confirmed (UTC) */
} sh_clock_now_t;

esp_err_t sh_clock_sync_init(void);         /* after nvs_flash_init() */
void sh_clock_sync_connected(void);         /* connected to the router: SNTP now */
/* The clock of the browser, seconds since 1970 (UTC); true: taken. */
bool sh_clock_sync_browser(uint32_t epoch_s);
void sh_clock_now(sh_clock_now_t *out);
esp_err_t sh_clock_set_zone(int zone);      /* index in sh_zones; stored, used at once */
