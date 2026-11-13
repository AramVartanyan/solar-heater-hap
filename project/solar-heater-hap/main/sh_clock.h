/* The clock (doc/operating-modes.md 3.1): SNTP (pool.ntp.org)
 * at every connection to the router and then every 12 h (CONFIG_LWIP_SNTP_UPDATE_DELAY); the time
 * of the browser, taken only without SNTP in the last 24 h; the time zone (default UTC) chosen or
 * confirmed by the installer, kept in NVS (namespace "sh_clock": the name and its POSIX TZ rule);
 * the check of the zone by the collector peak (a learned value, kept in NVS too). The rules:
 * sh_clock_rules. Not set after a power cut (no battery) until SNTP or the browser. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "sh_clock_rules.h"
#include "sh_day.h"
#include "sh_tz.h"

typedef struct {
    bool set;                   /* set since boot */
    bool sntp;                  /* the last setting came from SNTP */
    sh_clock_trust_t trust;
    sh_local_t lt;              /* local date and minute (lt.set == set) */
    time_t epoch;
    char tz[SH_TZ_NAME_MAX];
    bool tz_confirmed;
    bool tz_dst;                /* the zone has daylight saving time ... */
    bool dst;                   /* ... and it is in force now (with a set clock) */
    sh_peak_t peak;
} sh_clock_info_t;

esp_err_t sh_clock_init(void);          /* after nvs_flash_init(): the zone and the peak check */
esp_err_t sh_clock_start(void);         /* after captive_wifi_init() (esp_netif): SNTP, not started */
void sh_clock_connected(void);          /* connected to the router: SNTP now (again) */

void sh_clock_get(sh_clock_info_t *out);

/* The time of the browser; true if it was taken. */
bool sh_clock_browser(int64_t epoch_s);

/* The installer confirms the zone sh_tz_list[tz]: reliable at once, the peak check starts again. */
esp_err_t sh_clock_set_tz(int tz);

/* A finished day (sh_history, at midnight): the peak check (only with a confirmed zone). */
void sh_clock_day(const sh_day_sum_t *day);

/* Reset of the learned values: the state of the zone check. */
esp_err_t sh_clock_reset_learned(void);
