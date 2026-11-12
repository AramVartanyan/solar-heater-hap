/* User menu transitions — see sh_menu.h. */

#include "sh_menu.h"

void sh_menu_update(sh_menu_t *m, uint32_t now_ms)
{
    if (m->mode == SH_MENU_SAVED && now_ms - m->mark_ms >= SH_MENU_SAVED_MS) {
        m->mode = SH_MENU_LIST;
    }
    if (m->mode != SH_MENU_NORMAL && now_ms - m->key_ms >= SH_MENU_TIMEOUT_MS) {
        m->mode = SH_MENU_NORMAL;       /* an unconfirmed value is dropped */
    }
}

sh_menu_act_t sh_menu_key(sh_menu_t *m, sh_key_t key, uint32_t now_ms, bool test_running,
                          const sh_settings_t *s)
{
    sh_menu_update(m, now_ms);
    const bool repeat = key == SH_KEY_PLUS_REPEAT || key == SH_KEY_MINUS_REPEAT;
    const int dir = key == SH_KEY_PLUS || key == SH_KEY_PLUS_REPEAT ? 1 : -1;

    switch (m->mode) {
    case SH_MENU_NORMAL:
        if (key == SH_KEY_MENU) {
            if (test_running) {
                return SH_MENU_ACT_STOP;
            }
            m->mode = SH_MENU_LIST;
            m->pos = 0;
            m->key_ms = now_ms;
            return SH_MENU_ACT_NONE;
        }
        if (key == SH_KEY_PLUS) {
            return SH_MENU_ACT_PLUS;
        }
        if (key == SH_KEY_MINUS && test_running) {
            return SH_MENU_ACT_MINUS;
        }
        return SH_MENU_ACT_NONE;

    case SH_MENU_LIST:
        m->key_ms = now_ms;
        if (key == SH_KEY_MENU) {
            if (m->pos == SH_MENU_END) {
                m->mode = SH_MENU_NORMAL;
            } else {
                m->mode = SH_MENU_EDIT;
                m->value = s->v[m->pos];
                m->mark_ms = now_ms;
            }
        } else if (!repeat) {
            m->pos = (uint8_t)((m->pos + SH_MENU_ITEMS + dir) % SH_MENU_ITEMS);
        }
        return SH_MENU_ACT_NONE;

    case SH_MENU_EDIT:
        m->key_ms = now_ms;
        if (key == SH_KEY_MENU) {
            m->mode = SH_MENU_SAVED;
            m->mark_ms = now_ms;
            return m->value != s->v[m->pos] ? SH_MENU_ACT_SAVE : SH_MENU_ACT_NONE;
        }
        m->value = sh_settings_step(s, (sh_setting_t)m->pos, m->value, dir);
        m->mark_ms = now_ms;            /* the new value is shown at once */
        return SH_MENU_ACT_NONE;

    case SH_MENU_SAVED:
    default:
        return SH_MENU_ACT_NONE;        /* the 1 s steady value: keys are ignored */
    }
}

void sh_menu_view(const sh_menu_t *m, uint32_t now_ms, sh_menu_view_t *out)
{
    out->mode = m->mode;
    out->pos = m->pos;
    out->value = m->value;
    out->value_visible = m->mode != SH_MENU_EDIT ||
                         (now_ms - m->mark_ms) % (2 * SH_MENU_BLINK_MS) < SH_MENU_BLINK_MS;
}

bool sh_menu_test_label(uint32_t elapsed_s)
{
    if (elapsed_s < SH_MENU_TEST_LABEL_S) {
        return true;
    }
    return (elapsed_s - SH_MENU_TEST_LABEL_S) % SH_MENU_TEST_PERIOD_S == SH_MENU_TEST_PERIOD_S - 1;
}
