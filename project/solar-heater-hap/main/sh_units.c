/* The units of the display — see sh_units.h. */

#include "sh_units.h"

/* n / d rounded half away from zero (d > 0) */
static int32_t div_round(int32_t n, int32_t d)
{
    return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
}

int16_t sh_units_temp10(int16_t c10, sh_units_t u)
{
    return u == SH_UNITS_F ? (int16_t)(div_round((int32_t)c10 * 9, 5) + 320) : c10;
}

int16_t sh_units_temp(int16_t c, sh_units_t u)
{
    return u == SH_UNITS_F ? (int16_t)(div_round((int32_t)c * 9, 5) + 32) : c;
}

int16_t sh_units_diff(int16_t k, sh_units_t u)
{
    return u == SH_UNITS_F ? (int16_t)div_round((int32_t)k * 9, 5) : k;
}
