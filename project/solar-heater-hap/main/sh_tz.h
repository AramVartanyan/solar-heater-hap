/* Supported time zones (doc/operating-modes.md 7) — plain C, compiled into the host
 * test too. The list lives in the firmware (nothing is loaded from outside): the IANA name the
 * browser reports (Intl ... timeZone) and its POSIX TZ rule with the daylight saving time. The page
 * gets the names (GET /api/tz) and sends back the index of the chosen one. */
#pragma once

#include <stddef.h>

#define SH_TZ_NAME_MAX      32      /* longest name with its NUL */
#define SH_TZ_RULE_MAX      48      /* longest rule with its NUL */

typedef struct {
    const char *name;               /* IANA name, e.g. "Europe/Sofia" */
    const char *rule;               /* POSIX TZ, e.g. "EET-2EEST,M3.5.0/3,M10.5.0/4" */
} sh_tz_t;

extern const sh_tz_t sh_tz_list[];
extern const size_t sh_tz_count;

#define SH_TZ_DEFAULT       0       /* UTC */

/* Index of a name in the list, -1 if it is not there. */
int sh_tz_find(const char *name);
