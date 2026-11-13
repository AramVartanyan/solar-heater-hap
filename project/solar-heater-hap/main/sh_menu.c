/* User menu transitions — see sh_menu.h. */

#include "sh_menu.h"

void sh_menu_update(sh_menu_t *m, uint32_t now_ms)
{
    /* after nEt -> AP the two halves of the access code instead of the 1 s steady value */
    const uint32_t saved_ms = m->pos == SH_MENU_NET && m->value != 0 ? 2 * SH_MENU_CODE_MS : SH_MENU_SAVED_MS;
    if (m->mode == SH_MENU_SAVED && now_ms - m->mark_ms >= saved_ms) {
        m->mode = SH_MENU_LIST;
    }
    if (m->mode != SH_MENU_NORMAL && now_ms - m->key_ms >= SH_MENU_TIMEOUT_MS) {
        m->mode = SH_MENU_NORMAL;       /* an unconfirmed value is dropped */
    }
}

sh_setting_t sh_menu_setting(uint8_t pos, bool winter)
{
    static const sh_setting_t ids[SH_MENU_NET] = {
        [SH_MENU_SSN] = SH_SET_SEASON, [SH_MENU_HOL] = SH_SET_VACATION, [SH_MENU_TBO] = SH_SET_TANK_MAX,
        [SH_MENU_CLO] = SH_SET_COL_MIN, [SH_MENU_DON] = SH_SET_DIFF_ON, [SH_MENU_DOF] = SH_SET_DIFF_OFF,
        [SH_MENU_TST] = SH_SET_TEST_S,
    };
    return sh_settings_seasonal(ids[pos < SH_MENU_NET ? pos : SH_MENU_TBO], winter);
}

sh_menu_act_t sh_menu_key(sh_menu_t *m, sh_key_t key, uint32_t now_ms, const sh_menu_ctx_t *ctx,
                          const sh_settings_t *s)
{
    sh_menu_update(m, now_ms);
    const bool repeat = key == SH_KEY_PLUS_REPEAT || key == SH_KEY_MINUS_REPEAT;
    const int dir = key == SH_KEY_PLUS || key == SH_KEY_PLUS_REPEAT ? 1 : -1;

    switch (m->mode) {
    case SH_MENU_NORMAL:
        if (key == SH_KEY_MENU) {
            if (ctx->fup_offer) {
                return SH_MENU_ACT_FUP;
            }
            if (ctx->test_running) {
                return SH_MENU_ACT_STOP;
            }
            if (ctx->air_running) {
                return SH_MENU_ACT_AIR_STOP;
            }
            m->mode = SH_MENU_LIST;
            m->pos = 0;
            m->key_ms = now_ms;
            return SH_MENU_ACT_NONE;
        }
        if (key == SH_KEY_PLUS && !ctx->air_running) {
            return SH_MENU_ACT_PLUS;
        }
        if (key == SH_KEY_MINUS && ctx->test_running) {
            return SH_MENU_ACT_MINUS;
        }
        return SH_MENU_ACT_NONE;

    case SH_MENU_LIST:
        m->key_ms = now_ms;
        if (key == SH_KEY_MENU) {
            if (m->pos == SH_MENU_END) {
                m->mode = SH_MENU_NORMAL;
            } else if (m->pos == SH_MENU_NET) {
                m->mode = SH_MENU_EDIT;
                m->value = ctx->net_ap;         /* the current mode */
                m->mark_ms = now_ms;
            } else {
                m->mode = SH_MENU_EDIT;
                m->id = (uint8_t)sh_menu_setting(m->pos, ctx->winter);
                m->value = s->v[m->id];
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
            if (m->pos == SH_MENU_NET) {
                if (m->value != 0 && ctx->net_connected) {
                    m->value = 0;               /* refused: "CP" steady for 1 s, no code */
                    return SH_MENU_ACT_NONE;
                }
                return SH_MENU_ACT_NET;
            }
            return m->value != s->v[m->id] ? SH_MENU_ACT_SAVE : SH_MENU_ACT_NONE;
        }
        if (m->pos == SH_MENU_NET) {
            m->value = dir > 0;                 /* "+" AP, "-" CP, no wrap */
        } else {
            m->value = sh_settings_step(s, (sh_setting_t)m->id, m->value, dir);
        }
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
    out->code_part = 0;
    if (m->mode == SH_MENU_SAVED && m->pos == SH_MENU_NET && m->value != 0) {
        out->code_part = now_ms - m->mark_ms < SH_MENU_CODE_MS ? 1 : 2;
    }
}

bool sh_menu_test_label(uint32_t elapsed_s)
{
    if (elapsed_s < SH_MENU_TEST_LABEL_S) {
        return true;
    }
    return (elapsed_s - SH_MENU_TEST_LABEL_S) % SH_MENU_TEST_PERIOD_S == SH_MENU_TEST_PERIOD_S - 1;
}

sh_menu_air_t sh_menu_air_screen(uint32_t elapsed_s)
{
    if (elapsed_s < SH_MENU_TEST_LABEL_S) {
        return SH_MENU_AIR_LABEL;
    }
    const uint32_t t = (elapsed_s - SH_MENU_TEST_LABEL_S) % SH_MENU_TEST_PERIOD_S;
    return t == 0 ? SH_MENU_AIR_PHASE : t == SH_MENU_TEST_PERIOD_S - 1 ? SH_MENU_AIR_LABEL : SH_MENU_AIR_LEFT;
}

sh_menu_label_t sh_menu_label_at(bool vacation, bool ap, bool fup, uint32_t idx)
{
    const sh_menu_label_t labels[] = { vacation ? SH_MENU_LABEL_HOL : SH_MENU_LABEL_NONE,
                                       ap ? SH_MENU_LABEL_AP : SH_MENU_LABEL_NONE,
                                       fup ? SH_MENU_LABEL_FUP : SH_MENU_LABEL_NONE };
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        if (labels[i] != SH_MENU_LABEL_NONE && idx-- == 0) {
            return labels[i];
        }
    }
    return SH_MENU_LABEL_NONE;
}
