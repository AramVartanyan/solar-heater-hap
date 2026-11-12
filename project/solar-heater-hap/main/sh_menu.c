/* User menu: transitions and display — see sh_menu.h. */

#include "sh_menu.h"

/* Raw segments, XGFEDCBA */
#define SEG_DOT     0x80
#define SEG_b       0x7C
#define SEG_C       0x39
#define SEG_d       0x5E
#define SEG_E       0x79
#define SEG_F       0x71
#define SEG_L       0x38
#define SEG_n       0x54
#define SEG_o       0x5C
#define SEG_S       0x6D
#define SEG_t       0x78

static const uint8_t s_digit[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

/* The list, in its order; the dot of "C.Lo", "d.on" and "d.oF" shares the first digit. */
static const uint8_t s_name[SH_MENU_POS_COUNT][SH_MENU_DIGITS] = {
    [SH_SET_TANK_MAX] = { SEG_t, SEG_b, SEG_o },
    [SH_SET_COL_MIN]  = { SEG_C | SEG_DOT, SEG_L, SEG_o },
    [SH_SET_DIFF_ON]  = { SEG_d | SEG_DOT, SEG_o, SEG_n },
    [SH_SET_DIFF_OFF] = { SEG_d | SEG_DOT, SEG_o, SEG_F },
    [SH_SET_TEST_S]   = { SEG_t, SEG_S, SEG_t },
    [SH_MENU_POS_END] = { SEG_E, SEG_n, SEG_d },
};

static void copy(uint8_t seg[SH_MENU_DIGITS], const uint8_t from[SH_MENU_DIGITS])
{
    for (int i = 0; i < SH_MENU_DIGITS; i++) {
        seg[i] = from[i];
    }
}

static void edit_key(sh_menu_t *m, sh_key_t key, uint32_t now_ms, const sh_settings_t *cur)
{
    const sh_setting_range_t r = sh_setting_range(cur, (sh_setting_id_t)m->pos);
    int32_t v = m->value;
    if (key == SH_KEY_PLUS || key == SH_KEY_PLUS_REPEAT) {
        v += r.step;
    } else {
        v -= r.step;
    }
    if (v > r.max) {
        v = r.max;
    }
    if (v < r.min) {
        v = r.min;
    }
    m->value = (int16_t)v;
    m->mark_ms = now_ms;        /* the new value is shown at once, the blinking starts over */
}

bool sh_menu_key(sh_menu_t *m, sh_key_t key, uint32_t now_ms, const sh_settings_t *cur,
                 sh_setting_id_t *id, int16_t *value)
{
    sh_menu_tick(m, now_ms);    /* a key after the timeout is a key on the normal screen */

    switch (m->state) {
    case SH_MENU_OFF:
        if (key != SH_KEY_MENU) {
            return false;
        }
        m->state = SH_MENU_LIST;
        m->pos = 0;
        break;

    case SH_MENU_LIST:
        if (key == SH_KEY_PLUS) {
            m->pos = (uint8_t)((m->pos + 1) % SH_MENU_POS_COUNT);
        } else if (key == SH_KEY_MINUS) {
            m->pos = (uint8_t)((m->pos + SH_MENU_POS_COUNT - 1) % SH_MENU_POS_COUNT);
        } else if (key == SH_KEY_MENU) {
            if (m->pos == SH_MENU_POS_END) {
                m->state = SH_MENU_OFF;
            } else {
                m->state = SH_MENU_EDIT;
                m->value = cur->v[m->pos];
                m->mark_ms = now_ms;
            }
        } else {
            return false;       /* auto-repeat does not run through the list */
        }
        break;

    case SH_MENU_EDIT:
        if (key == SH_KEY_MENU) {
            const bool changed = m->value != cur->v[m->pos];
            m->state = SH_MENU_SAVED;
            m->mark_ms = now_ms;
            m->key_ms = now_ms;
            if (changed) {
                *id = (sh_setting_id_t)m->pos;
                *value = m->value;
            }
            return changed;
        }
        edit_key(m, key, now_ms, cur);
        break;

    case SH_MENU_SAVED:
        return false;           /* keys wait for the list */
    }

    m->key_ms = now_ms;
    return false;
}

void sh_menu_tick(sh_menu_t *m, uint32_t now_ms)
{
    if (m->state == SH_MENU_SAVED && now_ms - m->mark_ms >= SH_MENU_SAVED_MS) {
        m->state = SH_MENU_LIST;
    }
    if ((m->state == SH_MENU_LIST || m->state == SH_MENU_EDIT) &&
        now_ms - m->key_ms >= SH_MENU_TIMEOUT_MS) {
        m->state = SH_MENU_OFF;
    }
}

void sh_menu_number(int value, uint8_t seg[SH_MENU_DIGITS])
{
    if (value < 0) {
        value = 0;
    }
    if (value > 999) {
        value = 999;
    }
    for (int i = SH_MENU_DIGITS - 1; i >= 0; i--) {
        seg[i] = (value != 0 || i == SH_MENU_DIGITS - 1) ? s_digit[value % 10] : 0x00;
        value /= 10;
    }
}

void sh_menu_render(const sh_menu_t *m, uint32_t now_ms, uint8_t seg[SH_MENU_DIGITS])
{
    switch (m->state) {
    case SH_MENU_EDIT:
        if (((now_ms - m->mark_ms) / SH_MENU_BLINK_MS) % 2 != 0) {
            const uint8_t blank[SH_MENU_DIGITS] = { 0 };
            copy(seg, blank);
            break;
        }
        /* fall through */
    case SH_MENU_SAVED:
        sh_menu_number(m->value, seg);
        break;
    default:
        copy(seg, s_name[m->pos < SH_MENU_POS_COUNT ? m->pos : SH_MENU_POS_END]);
        break;
    }
}

void sh_menu_test_label(uint8_t seg[SH_MENU_DIGITS])
{
    copy(seg, s_name[SH_SET_TEST_S]);
}

bool sh_menu_test_shows_label(uint32_t elapsed_ms)
{
    if (elapsed_ms < SH_MENU_TEST_LABEL_MS) {
        return true;
    }
    /* 6 s of the time left, then the label for 1 s */
    return (elapsed_ms - SH_MENU_TEST_LABEL_MS) % SH_MENU_TEST_PERIOD_MS >=
           SH_MENU_TEST_PERIOD_MS - SH_MENU_TEST_AGAIN_MS;
}
