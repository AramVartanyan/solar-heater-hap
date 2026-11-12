/* Clock rules — see sh_clock_rules.h. */

#include "sh_clock_rules.h"

bool sh_clock_browser_ok(int64_t epoch_s, bool sntp_ever, uint32_t sntp_s, uint32_t now_s)
{
    if (epoch_s < SH_CLOCK_EPOCH_MIN || epoch_s >= SH_CLOCK_EPOCH_MAX) {
        return false;
    }
    return !sntp_ever || now_s - sntp_s >= SH_CLOCK_BROWSER_HOLD_S;
}

sh_clock_trust_t sh_clock_trust(bool set, bool tz_confirmed, bool peak_reliable)
{
    if (!set) {
        return SH_CLOCK_UNSET;
    }
    if (!tz_confirmed) {
        return SH_CLOCK_TZ_UNCONFIRMED;
    }
    return peak_reliable ? SH_CLOCK_RELIABLE : SH_CLOCK_UNRELIABLE;
}

void sh_peak_reset(sh_peak_t *p, bool reliable)
{
    p->reliable = reliable;
    p->in_row = 0;
    p->out_row = 0;
    p->peak = SH_PEAK_NONE;
}

sh_peak_day_t sh_peak_day(sh_peak_t *p, bool full, bool col_ok, int16_t col_min, int16_t col_max,
                          int16_t peak_min)
{
    if (!full || !col_ok || peak_min < 0 || col_max - col_min < SH_PEAK_SUNNY_RISE) {
        return SH_PEAK_NOT_COUNTED;
    }
    p->peak = peak_min;
    if (peak_min >= SH_PEAK_FROM_MIN && peak_min <= SH_PEAK_TO_MIN) {
        p->out_row = 0;
        if (p->in_row < SH_PEAK_DAYS) {
            p->in_row++;
        }
        if (p->in_row >= SH_PEAK_DAYS) {
            p->reliable = true;
        }
        return SH_PEAK_IN;
    }
    p->in_row = 0;
    if (p->out_row < SH_PEAK_DAYS) {
        p->out_row++;
    }
    if (p->out_row >= SH_PEAK_DAYS) {
        p->reliable = false;
    }
    return SH_PEAK_OUT;
}

/* Days since 1970-01-01 of a civil date (H. Hinnant, "days_from_civil"). */
static long days_from_civil(long y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

#define DAY_2020    18262L      /* days_from_civil(2020, 1, 1) */

uint16_t sh_day_number(int year, int month, int mday)
{
    if (year < 2020 || year > 2099 || month < 1 || month > 12 || mday < 1 || mday > 31) {
        return SH_DAY_NONE;
    }
    return (uint16_t)(days_from_civil(year, (unsigned)month, (unsigned)mday) - DAY_2020);
}

void sh_day_date(uint16_t day, int *year, int *month, int *mday)
{
    /* civil_from_days */
    const long z = (long)day + DAY_2020 + 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    *year = (int)((long)yoe + era * 400 + (m <= 2));
    *month = (int)m;
    *mday = (int)d;
}
