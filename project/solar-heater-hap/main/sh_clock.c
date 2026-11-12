/* Clock rules — see sh_clock.h. */

#include <string.h>
#include "sh_clock.h"

bool sh_clock_browser_ok(bool sntp, uint32_t since_sntp_s)
{
    return !sntp || since_sntp_s >= SH_CLOCK_SNTP_FRESH_S;
}

/* ---- time zones ------------------------------------------------------------------------- */

#define TZ_WET  "WET0WEST,M3.5.0/1,M10.5.0"
#define TZ_CET  "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_EET  "EET-2EEST,M3.5.0/3,M10.5.0/4"
#define TZ_US(std, dst)  std dst ",M3.2.0,M11.1.0"

/* UTC first, then by name. A zone the list does not have: the nearest one with the same rule. */
const sh_zone_t sh_zones[] = {
    { "UTC",                    "UTC0" },
    { "Africa/Cairo",           "EET-2EEST,M4.5.5/0,M10.5.4/24" },
    { "Africa/Johannesburg",    "SAST-2" },
    { "Africa/Lagos",           "WAT-1" },
    { "Africa/Nairobi",         "EAT-3" },
    { "America/Anchorage",      TZ_US("AKST9", "AKDT") },
    { "America/Bogota",         "<-05>5" },
    { "America/Chicago",        TZ_US("CST6", "CDT") },
    { "America/Denver",         TZ_US("MST7", "MDT") },
    { "America/Halifax",        TZ_US("AST4", "ADT") },
    { "America/Los_Angeles",    TZ_US("PST8", "PDT") },
    { "America/Mexico_City",    "CST6" },
    { "America/New_York",       TZ_US("EST5", "EDT") },
    { "America/Phoenix",        "MST7" },
    { "America/Santiago",       "<-04>4<-03>,M9.1.6/24,M4.1.6/24" },
    { "America/Sao_Paulo",      "<-03>3" },
    { "America/Toronto",        TZ_US("EST5", "EDT") },
    { "America/Vancouver",      TZ_US("PST8", "PDT") },
    { "Asia/Baku",              "<+04>-4" },
    { "Asia/Bangkok",           "<+07>-7" },
    { "Asia/Dhaka",             "<+06>-6" },
    { "Asia/Dubai",             "<+04>-4" },
    { "Asia/Jerusalem",         "IST-2IDT,M3.4.4/26,M10.5.0" },
    { "Asia/Karachi",           "PKT-5" },
    { "Asia/Kolkata",           "IST-5:30" },
    { "Asia/Nicosia",           TZ_EET },
    { "Asia/Seoul",             "KST-9" },
    { "Asia/Shanghai",          "CST-8" },
    { "Asia/Singapore",         "<+08>-8" },
    { "Asia/Tbilisi",           "<+04>-4" },
    { "Asia/Tehran",            "<+0330>-3:30" },
    { "Asia/Tokyo",             "JST-9" },
    { "Asia/Yerevan",           "<+04>-4" },
    { "Atlantic/Canary",        TZ_WET },
    { "Atlantic/Reykjavik",     "GMT0" },
    { "Australia/Adelaide",     "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
    { "Australia/Brisbane",     "AEST-10" },
    { "Australia/Perth",        "AWST-8" },
    { "Australia/Sydney",       "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Europe/Amsterdam",       TZ_CET },
    { "Europe/Athens",          TZ_EET },
    { "Europe/Belgrade",        TZ_CET },
    { "Europe/Berlin",          TZ_CET },
    { "Europe/Bratislava",      TZ_CET },
    { "Europe/Brussels",        TZ_CET },
    { "Europe/Bucharest",       TZ_EET },
    { "Europe/Budapest",        TZ_CET },
    { "Europe/Copenhagen",      TZ_CET },
    { "Europe/Dublin",          "GMT0IST,M3.5.0/1,M10.5.0" },
    { "Europe/Helsinki",        TZ_EET },
    { "Europe/Istanbul",        "<+03>-3" },
    { "Europe/Kyiv",            TZ_EET },
    { "Europe/Lisbon",          TZ_WET },
    { "Europe/Ljubljana",       TZ_CET },
    { "Europe/London",          "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Luxembourg",      TZ_CET },
    { "Europe/Madrid",          TZ_CET },
    { "Europe/Malta",           TZ_CET },
    { "Europe/Moscow",          "MSK-3" },
    { "Europe/Oslo",            TZ_CET },
    { "Europe/Paris",           TZ_CET },
    { "Europe/Podgorica",       TZ_CET },
    { "Europe/Prague",          TZ_CET },
    { "Europe/Riga",            TZ_EET },
    { "Europe/Rome",            TZ_CET },
    { "Europe/Sarajevo",        TZ_CET },
    { "Europe/Skopje",          TZ_CET },
    { "Europe/Sofia",           TZ_EET },
    { "Europe/Stockholm",       TZ_CET },
    { "Europe/Tallinn",         TZ_EET },
    { "Europe/Tirane",          TZ_CET },
    { "Europe/Vienna",          TZ_CET },
    { "Europe/Vilnius",         TZ_EET },
    { "Europe/Warsaw",          TZ_CET },
    { "Europe/Zagreb",          TZ_CET },
    { "Europe/Zurich",          TZ_CET },
    { "Pacific/Auckland",       "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    { "Pacific/Honolulu",       "HST10" },
};
const int sh_zone_count = (int)(sizeof(sh_zones) / sizeof(sh_zones[0]));

int sh_zone_find(const char *name)
{
    for (int i = 0; name && i < sh_zone_count; i++) {
        if (strcmp(name, sh_zones[i].name) == 0) {
            return i;
        }
    }
    return SH_ZONE_NONE;
}

/* ---- dates ------------------------------------------------------------------------------ */

#define EPOCH_2020      18262L      /* 2020-01-01, days since 1970-01-01 */

/* Days since 1970-01-01 of a date of the Gregorian calendar, and back (whole eras of 400 years). */
static long days_from_civil(long y, int m, int d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

uint16_t sh_clock_day(int year, int month, int day)
{
    const long n = days_from_civil(year, month, day) - EPOCH_2020;
    return n < 0 ? 0 : n > UINT16_MAX ? UINT16_MAX : (uint16_t)n;
}

void sh_clock_date(uint16_t day_no, int *year, int *month, int *day)
{
    const long z = (long)day_no + EPOCH_2020 + 719468;
    const long era = z / 146097;
    const long doe = z - era * 146097;
    const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long mp = (5 * doy + 2) / 153;
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *year = (int)(yoe + era * 400 + (*month <= 2));
}

/* ---- peak check ------------------------------------------------------------------------- */

void sh_peak_reset(sh_peak_t *p, bool zone_confirmed)
{
    memset(p, 0, sizeof(*p));
    p->reliable = zone_confirmed;
    p->last_min = SH_PEAK_NONE;
}

void sh_peak_day(sh_peak_t *p, int16_t col_max, int16_t col_min, uint16_t peak_min)
{
    if (peak_min == SH_PEAK_NONE || col_max - col_min < SH_PEAK_SUNNY) {
        return;                         /* cloudy: neither counts nor breaks the row */
    }
    p->last_min = peak_min;
    p->last_in = peak_min >= SH_PEAK_FROM_MIN && peak_min <= SH_PEAK_TO_MIN;
    if (p->last_in) {
        p->out_row = 0;
        if (p->in_row < SH_PEAK_DAYS) {
            p->in_row++;
        }
        if (p->in_row >= SH_PEAK_DAYS) {
            p->reliable = true;
        }
    } else {
        p->in_row = 0;
        if (p->out_row < SH_PEAK_DAYS) {
            p->out_row++;
        }
        if (p->out_row >= SH_PEAK_DAYS) {
            p->reliable = false;
        }
    }
}
