/*
 * home.c — Home screen
 *
 * Layout (800×480):
 *
 *  ┌─────────────────────────────────────────────────────────────┐
 *  │  ⚡ FIBER NODE          [●] Connected   v0.7.0   [SIGNER]  │  ← status bar
 *  ├──────────────────┬──────────────────┬───────────────────────┤
 *  │                  │                  │                       │
 *  │  NODE ID         │   PEERS          │   CHANNELS            │
 *  │  026a9d...       │      21          │       3               │
 *  │                  │                  │                       │
 *  ├──────────────────┴──────────────────┴───────────────────────┤
 *  │                                                             │
 *  │         Total local balance: 9,850.00 CKB                  │
 *  │                                                             │
 *  ├─────────────────────────┬───────────────────────────────────┤
 *  │                         │                                   │
 *  │     [ SEND  ▶ ]        │       [ ◀ RECEIVE ]               │
 *  │                         │                                   │
 *  ├─────────────────────────┴───────────────────────────────────┤
 *  │     [ CHANNELS ]          [ SIGNER ]       last sync: 3s   │
 *  └─────────────────────────────────────────────────────────────┘
 */

#include "screens.h"
#include "../core/state.h"
#include "../core/bridge.h"
#include "lvgl/lvgl.h"

/* ── Refs ─────────────────────────────────────────────────────────── */
static lv_obj_t *scr;
static lv_obj_t *lbl_node_id;
static lv_obj_t *lbl_peers;
static lv_obj_t *lbl_channels;
static lv_obj_t *lbl_balance;
static lv_obj_t *lbl_status;
static lv_obj_t *lbl_sync;
static lv_obj_t *dot_connected;

/* ── Theme colours ───────────────────────────────────────────────── */
#define COL_BG        lv_color_hex(0x0d0f14)
#define COL_SURFACE   lv_color_hex(0x161a22)
#define COL_ACCENT    lv_color_hex(0x3bc67a)
#define COL_ACCENT2   lv_color_hex(0x4a9eff)
#define COL_WARN      lv_color_hex(0xf5a623)
#define COL_DANGER    lv_color_hex(0xe74c3c)
#define COL_TEXT      lv_color_hex(0xe2e8f0)
#define COL_MUTED     lv_color_hex(0x64748b)
#define COL_BORDER    lv_color_hex(0x252b38)

/* ── Helpers ─────────────────────────────────────────────────────── */
static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COL_SURFACE, 0);
    lv_obj_set_style_border_color(card, COL_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, lv_color_t col,
                              lv_align_t align) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, col, 0);
    lv_obj_align(lbl, align, 0, 0);
    return lbl;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *label,
                            lv_color_t bg, int x, int y, int w, int h,
                            lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(bg, 40), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_center(lbl);
    return btn;
}

/* ── Event callbacks ─────────────────────────────────────────────── */
static void on_send(lv_event_t *e)     { (void)e; fk_nav_to(SCREEN_CHANNELS); } /* channels first */
static void on_receive(lv_event_t *e)  { (void)e; fk_nav_to(SCREEN_RECEIVE); }
static void on_channels(lv_event_t *e) { (void)e; fk_nav_to(SCREEN_CHANNELS); }
static void on_signer(lv_event_t *e)   { (void)e; fk_nav_to(SCREEN_SIGNER); }

/* ── Build ───────────────────────────────────────────────────────── */
lv_obj_t *home_build(void) {
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Top status bar ── */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 800, 44);
    lv_obj_set_style_bg_color(bar, COL_SURFACE, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, COL_BORDER, 0);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_left(bar, 16, 0);
    lv_obj_set_style_pad_right(bar, 16, 0);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, LV_SYMBOL_CHARGE " FIBER NODE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COL_WARN, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lbl_status = lv_label_create(bar);
    lv_label_set_text(lbl_status, "● Connecting...");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_status, COL_MUTED, 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_signer_bar = lv_btn_create(bar);
    lv_obj_set_size(btn_signer_bar, 80, 28);
    lv_obj_align(btn_signer_bar, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_signer_bar, COL_BORDER, 0);
    lv_obj_set_style_radius(btn_signer_bar, 6, 0);
    lv_obj_add_event_cb(btn_signer_bar, on_signer, LV_EVENT_CLICKED, NULL);
    lv_obj_t *signer_lbl = lv_label_create(btn_signer_bar);
    lv_label_set_text(signer_lbl, "SIGNER");
    lv_obj_set_style_text_font(signer_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(signer_lbl, COL_TEXT, 0);
    lv_obj_center(signer_lbl);

    /* ── Stat cards row ── */
    /* Node ID card */
    lv_obj_t *card_id = make_card(scr, 12, 56, 250, 80);
    lv_obj_t *lbl_id_title = lv_label_create(card_id);
    lv_label_set_text(lbl_id_title, "NODE ID");
    lv_obj_set_style_text_font(lbl_id_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_id_title, COL_MUTED, 0);
    lv_obj_align(lbl_id_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lbl_node_id = lv_label_create(card_id);
    lv_label_set_text(lbl_node_id, "—");
    lv_label_set_long_mode(lbl_node_id, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_node_id, 226);
    lv_obj_set_style_text_font(lbl_node_id, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_node_id, COL_ACCENT2, 0);
    lv_obj_align(lbl_node_id, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Peers card */
    lv_obj_t *card_peers = make_card(scr, 274, 56, 160, 80);
    lv_obj_t *lbl_peers_t = lv_label_create(card_peers);
    lv_label_set_text(lbl_peers_t, "PEERS");
    lv_obj_set_style_text_font(lbl_peers_t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_peers_t, COL_MUTED, 0);
    lv_obj_align(lbl_peers_t, LV_ALIGN_TOP_LEFT, 0, 0);
    lbl_peers = lv_label_create(card_peers);
    lv_label_set_text(lbl_peers, "—");
    lv_obj_set_style_text_font(lbl_peers, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_peers, COL_TEXT, 0);
    lv_obj_align(lbl_peers, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Channels card */
    lv_obj_t *card_ch = make_card(scr, 446, 56, 160, 80);
    lv_obj_t *lbl_ch_t = lv_label_create(card_ch);
    lv_label_set_text(lbl_ch_t, "CHANNELS");
    lv_obj_set_style_text_font(lbl_ch_t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_ch_t, COL_MUTED, 0);
    lv_obj_align(lbl_ch_t, LV_ALIGN_TOP_LEFT, 0, 0);
    lbl_channels = lv_label_create(card_ch);
    lv_label_set_text(lbl_channels, "—");
    lv_obj_set_style_text_font(lbl_channels, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_channels, COL_ACCENT, 0);
    lv_obj_align(lbl_channels, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Balance card (full width) */
    lv_obj_t *card_bal = make_card(scr, 12, 148, 776, 70);
    lv_obj_t *lbl_bal_t = lv_label_create(card_bal);
    lv_label_set_text(lbl_bal_t, "TOTAL LOCAL BALANCE");
    lv_obj_set_style_text_font(lbl_bal_t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_bal_t, COL_MUTED, 0);
    lv_obj_align(lbl_bal_t, LV_ALIGN_TOP_LEFT, 0, 0);
    lbl_balance = lv_label_create(card_bal);
    lv_label_set_text(lbl_balance, "— CKB");
    lv_obj_set_style_text_font(lbl_balance, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_balance, COL_WARN, 0);
    lv_obj_align(lbl_balance, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* ── Send / Receive buttons ── */
    make_btn(scr, LV_SYMBOL_RIGHT " SEND",    COL_ACCENT,  12,  232, 383, 90, on_send);
    make_btn(scr, "RECEIVE " LV_SYMBOL_LEFT,  COL_ACCENT2, 405, 232, 383, 90, on_receive);

    /* ── Bottom nav ── */
    make_btn(scr, LV_SYMBOL_LIST " CHANNELS", COL_SURFACE, 12,  336, 290, 56, on_channels);
    make_btn(scr, LV_SYMBOL_SETTINGS " SIGNER", COL_SURFACE, 314, 336, 290, 56, on_signer);

    lbl_sync = lv_label_create(scr);
    lv_label_set_text(lbl_sync, "last sync: —");
    lv_obj_set_style_text_font(lbl_sync, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_sync, COL_MUTED, 0);
    lv_obj_align(lbl_sync, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

    return scr;
}

/* ── Refresh (called from poll thread result) ────────────────────── */
void home_refresh(const FkState *s) {
    if (!scr) return;

    /* Status dot + text */
    if (s->bridge_ok) {
        lv_label_set_text_fmt(lbl_status, "#3bc67a ●# Connected  v%s", s->node_version);
    } else {
        lv_label_set_text(lbl_status, "#e74c3c ●# Disconnected");
    }
    lv_obj_set_style_text_color(lbl_status, COL_TEXT, 0);

    /* Node ID — truncate middle */
    if (s->node_id[0]) {
        char short_id[24];
        snprintf(short_id, sizeof(short_id), "%.8s...%.8s",
                 s->node_id, s->node_id + strlen(s->node_id) - 8);
        lv_label_set_text(lbl_node_id, short_id);
    }

    /* Peers / channels */
    lv_label_set_text_fmt(lbl_peers,   "%d", s->peer_count);
    lv_label_set_text_fmt(lbl_channels, "%d", s->channel_count);

    /* Total local balance */
    int64_t total = 0;
    for (int i = 0; i < s->channel_len; i++)
        total += s->channels[i].local_balance_ckb;
    /* local_balance is in shannons — convert to CKB */
    double ckb = (double)total / 1e8;
    lv_label_set_text_fmt(lbl_balance, "%.4f CKB", ckb);

    /* Sync time */
    lv_label_set_text_fmt(lbl_sync, "last sync: %lus ago",
                          (unsigned long)((lv_tick_get() - s->last_update_ms) / 1000));
}
