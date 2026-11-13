/* Created by Aram Vartanyan (c) 2026 */

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
    if (m->notice != SH_MENU_NOTICE_NONE && now_ms - m->notice_ms >= SH_MENU_NOTICE_MS) {
        m->notice = SH_MENU_NOTICE_NONE;
    }
}

static void notice(sh_menu_t *m, sh_menu_notice_t n, uint32_t now_ms)
{
    m->notice = (uint8_t)n;
    m->notice_ms = now_ms;
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

    if (ctx->locked) {                  /* the keyboard lock: no button does anything */
        m->mode = SH_MENU_NORMAL;       /* (a lock closes the menu: it is closed already) */
        notice(m, SH_MENU_NOTICE_LOC, now_ms);
        return SH_MENU_ACT_NONE;
    }

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
            } else if (m->pos == SH_MENU_HAP) {
                m->mode = SH_MENU_EDIT;
                m->value = ctx->hap_on;         /* the current setting */
                m->mark_ms = now_ms;
            } else if (m->pos == SH_MENU_LOC) {
                m->mode = SH_MENU_EDIT;
                m->value = 0;                   /* always "oFF": opened by chance, nothing locks */
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
            if (m->pos == SH_MENU_LOC) {
                if (m->value == 0) {
                    return SH_MENU_ACT_NONE;    /* "oFF" steady for 1 s, then the list */
                }
                m->mode = SH_MENU_NORMAL;       /* locked: the menu closes, "LoC" follows */
                return SH_MENU_ACT_LOCK;
            }
            if (m->pos == SH_MENU_HAP) {
                if ((m->value != 0) == ctx->hap_on) {
                    return SH_MENU_ACT_NONE;
                }
                if (m->value == 0 && ctx->hap_paired) {
                    m->value = 1;               /* refused: "on" steady for 1 s */
                    return SH_MENU_ACT_NONE;
                }
                return SH_MENU_ACT_HAP;
            }
            return m->value != s->v[m->id] ? SH_MENU_ACT_SAVE : SH_MENU_ACT_NONE;
        }
        if (m->pos == SH_MENU_NET || m->pos == SH_MENU_HAP || m->pos == SH_MENU_LOC) {
            m->value = dir > 0;                 /* "+" AP / on, "-" CP / oFF, no wrap */
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
    out->notice = now_ms - m->notice_ms < SH_MENU_NOTICE_MS ? (sh_menu_notice_t)m->notice : SH_MENU_NOTICE_NONE;
    if (m->mode == SH_MENU_SAVED && m->pos == SH_MENU_NET && m->value != 0) {
        out->code_part = now_ms - m->mark_ms < SH_MENU_CODE_MS ? 1 : 2;
    }
}

sh_menu_act_t sh_menu_chord(const sh_menu_t *m, const sh_menu_ctx_t *ctx)
{
    if (ctx->locked) {
        return SH_MENU_ACT_UNLOCK;      /* the one thing a locked keyboard does */
    }
    if (m->mode != SH_MENU_NORMAL || ctx->test_running || ctx->air_running) {
        return SH_MENU_ACT_NONE;
    }
    return SH_MENU_ACT_LOCK;
}

bool sh_menu_hold(sh_menu_t *m, uint32_t now_ms, const sh_menu_ctx_t *ctx)
{
    if (!ctx->locked) {
        return true;
    }
    notice(m, SH_MENU_NOTICE_LOC, now_ms);
    return false;
}

void sh_menu_lock_changed(sh_menu_t *m, bool locked, uint32_t now_ms)
{
    if (locked) {
        m->mode = SH_MENU_NORMAL;       /* an unconfirmed value is dropped */
    }
    notice(m, locked ? SH_MENU_NOTICE_LOC : SH_MENU_NOTICE_UNL, now_ms);
}

void sh_chord_button(sh_chord_t *c, bool plus, bool down, uint32_t now_ms)
{
    if (plus) {
        c->plus = down;
    } else {
        c->minus = down;
    }
    if (!down) {
        return;                         /* the mark stays for the tap that follows the release */
    }
    if (plus) {
        c->plus_chord = false;          /* a new press */
    } else {
        c->minus_chord = false;
    }
    if (c->plus && c->minus) {
        c->plus_chord = c->minus_chord = true;
        c->fired = false;
        c->both_ms = now_ms;
    }
}

bool sh_chord_holding(const sh_chord_t *c)
{
    return c->plus && c->minus;
}

bool sh_chord_step(sh_chord_t *c, uint32_t now_ms)
{
    if (!c->plus || !c->minus || c->fired || now_ms - c->both_ms < SH_MENU_CHORD_MS) {
        return false;
    }
    c->fired = true;
    return true;
}

bool sh_chord_swallow(const sh_chord_t *c, bool plus)
{
    return plus ? c->plus_chord : c->minus_chord;
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
