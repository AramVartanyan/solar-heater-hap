/* Supported time zones — see sh_tz.h. Rules: the POSIX TZ footer of the tz database files. */

#include <string.h>
#include "sh_tz.h"

#define CET     "CET-1CEST,M3.5.0,M10.5.0/3"
#define EET     "EET-2EEST,M3.5.0/3,M10.5.0/4"
#define US(z)   z ",M3.2.0,M11.1.0"

const sh_tz_t sh_tz_list[] = {
    { "UTC",                    "UTC0" },
    /* Europe */
    { "Europe/Sofia",           EET },
    { "Europe/Athens",          EET },
    { "Europe/Bucharest",       EET },
    { "Europe/Helsinki",        EET },
    { "Europe/Kyiv",            EET },
    { "Europe/Kiev",            EET },      /* the old name, still reported by some systems */
    { "Europe/Riga",            EET },
    { "Europe/Tallinn",         EET },
    { "Europe/Vilnius",         EET },
    { "Europe/Chisinau",        EET },
    { "Asia/Nicosia",           EET },
    { "Europe/Istanbul",        "<+03>-3" },
    { "Europe/Minsk",           "<+03>-3" },
    { "Europe/Moscow",          "MSK-3" },
    { "Europe/Belgrade",        CET },
    { "Europe/Skopje",          CET },
    { "Europe/Podgorica",       CET },
    { "Europe/Sarajevo",        CET },
    { "Europe/Zagreb",          CET },
    { "Europe/Ljubljana",       CET },
    { "Europe/Tirane",          CET },
    { "Europe/Budapest",        CET },
    { "Europe/Vienna",          CET },
    { "Europe/Bratislava",      CET },
    { "Europe/Prague",          CET },
    { "Europe/Warsaw",          CET },
    { "Europe/Berlin",          CET },
    { "Europe/Zurich",          CET },
    { "Europe/Rome",            CET },
    { "Europe/Malta",           CET },
    { "Europe/Paris",           CET },
    { "Europe/Brussels",        CET },
    { "Europe/Amsterdam",       CET },
    { "Europe/Luxembourg",      CET },
    { "Europe/Copenhagen",      CET },
    { "Europe/Oslo",            CET },
    { "Europe/Stockholm",       CET },
    { "Europe/Madrid",          CET },
    { "Europe/Lisbon",          "WET0WEST,M3.5.0/1,M10.5.0" },
    { "Atlantic/Canary",        "WET0WEST,M3.5.0/1,M10.5.0" },
    { "Europe/London",          "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Dublin",          "GMT0IST,M3.5.0/1,M10.5.0" },
    /* Asia, Africa, Oceania */
    { "Asia/Tbilisi",           "<+04>-4" },
    { "Asia/Yerevan",           "<+04>-4" },
    { "Asia/Baku",              "<+04>-4" },
    { "Asia/Dubai",             "<+04>-4" },
    { "Asia/Jerusalem",         "IST-2IDT,M3.4.4/26,M10.5.0" },
    { "Asia/Kolkata",           "IST-5:30" },
    { "Asia/Shanghai",          "CST-8" },
    { "Asia/Singapore",         "<+08>-8" },
    { "Asia/Tokyo",             "JST-9" },
    { "Africa/Johannesburg",    "SAST-2" },
    { "Australia/Sydney",       "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Australia/Perth",        "AWST-8" },
    { "Asia/Karachi",           "PKT-5" },              /* the rest of this group: step 9, from the */
    { "Asia/Dhaka",             "<+06>-6" },            /* other experiment variant (PR #35); the  */
    { "Asia/Bangkok",           "<+07>-7" },            /* rules shared by both lists are the same */
    { "Asia/Seoul",             "KST-9" },
    { "Asia/Tehran",            "<+0330>-3:30" },
    { "Africa/Cairo",           "EET-2EEST,M4.5.5/0,M10.5.4/24" },
    { "Africa/Lagos",           "WAT-1" },
    { "Africa/Nairobi",         "EAT-3" },
    { "Australia/Adelaide",     "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
    { "Australia/Brisbane",     "AEST-10" },
    { "Pacific/Auckland",       "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    { "Pacific/Honolulu",       "HST10" },
    /* America */
    { "America/New_York",       US("EST5EDT") },
    { "America/Toronto",        US("EST5EDT") },
    { "America/Chicago",        US("CST6CDT") },
    { "America/Denver",         US("MST7MDT") },
    { "America/Phoenix",        "MST7" },
    { "America/Los_Angeles",    US("PST8PDT") },
    { "America/Sao_Paulo",      "<-03>3" },
    { "America/Vancouver",      US("PST8PDT") },
    { "America/Anchorage",      US("AKST9AKDT") },
    { "America/Halifax",        US("AST4ADT") },
    { "America/Mexico_City",    "CST6" },
    { "America/Bogota",         "<-05>5" },
    { "America/Santiago",       "<-04>4<-03>,M9.1.6/24,M4.1.6/24" },
    { "Atlantic/Reykjavik",     "GMT0" },
};

const size_t sh_tz_count = sizeof(sh_tz_list) / sizeof(sh_tz_list[0]);

int sh_tz_find(const char *name)
{
    for (size_t i = 0; name && i < sh_tz_count; i++) {
        if (strcmp(sh_tz_list[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}
