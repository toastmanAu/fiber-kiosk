/*
 * send.c — Send payment screen (amount entry + invoice paste)
 *
 *  ┌──────────────────────────────────────────────────────────────┐
 *  │ ← BACK            SEND PAYMENT                              │
 *  ├──────────────────────────────────────────────────────────────┤
 *  │  Via channel: 026a9d... ↔ 0301ae...   Local: 9,500 CKB     │
 *  ├─────────────────────┬────────────────────────────────────────┤
 *  │                     │  Paste invoice:                       │
 *  │  [  7  ] [ 8  ] [9] │  ┌──────────────────────────────┐   │
 *  │  [  4  ] [ 5  ] [6] │  │ ckbinv1...                  │   │
 *  │  [  1  ] [ 2  ] [3] │  └──────────────────────────────┘   │
 *  │  [     0     ] [⌫] │                                       │
 *  │                     │  Amount: 100.00 CKB                  │
 *  │                     │  Fee: ≤ 0.1 CKB                      │
 *  │                     │                                       │
 *  │                     │       [ SEND → ]                     │
 *  └──────────────────────────────────────────────────────────────┘
 */

#include "screens.h"
#include "../core/state.h"
#include "../core/bridge.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define COL_BG      lv_color_hex(0x0d0f14)
#define COL_SURFACE lv_color_hex(0x161a22)
#define COL_ACCENT  lv_color_hex(0x3bc67a)
#define COL_WARN    lv_color_hex(0xf5a623)
#define COL_TEXT    lv_color_hex(0xe2e8f0)
#define COL_MUTED   lv_color_hex(0x64748b)
#define COL_BORDER  lv_color_hex(0x252b38)
#define COL_DANGER  lv_color_hex(0xe74c3c)

static lv_obj_t *scr;
static lv_obj_t *lbl_channel_info;
static lv_obj_t *ta_invoice;
static lv_obj_t *lbl_amount;
static lv_obj_t *lbl_fee;
static lv_obj_t *lbl_err;

static char s_invoice[512];

static void on_back(lv_event_t *e)  { (void)e; fk_nav_to(SCREEN_CHANNELS); }

static void on_send_tap(lv_event_t *e) {
    (void)e;
    const char *inv = lv_textarea_get_text(ta_invoice);
    if (!inv || strlen(inv) < 10) {
        lv_label_set_text(lbl_err, "No invoice entered");
        return;
    }
    strncpy(s_invoice, inv, sizeof(s_invoice)-1);

    /* Save invoice to state for confirm screen */
    pthread_mutex_lock(&g_state_mutex);
    strncpy(g_state.active_invoice, s_invoice, sizeof(g_state.active_invoice)-1);
    pthread_mutex_unlock(&g_state_mutex);

    fk_nav_to(SCREEN_CONFIRM);
}

static void on_invoice_changed(lv_event_t *e) {
    (void)e;
    const char *text = lv_textarea_get_text(ta_invoice);
    if (text && strlen(text) > 20) {
        /* TODO: parse invoice amount when we have the bridge client here */
        lv_label_set_text(lbl_amount, "Amount: (parsing...)");
        lv_label_set_text(lbl_err, "");
    }
}

lv_obj_t *send_build(void) {
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Header */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_pos(bar, 0, 0); lv_obj_set_size(bar, 800, 44);
    lv_obj_set_style_bg_color(bar, COL_SURFACE, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, COL_BORDER, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(bar);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(btn_back, COL_BORDER, 0);
    lv_obj_set_style_radius(btn_back, 6, 0);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn_back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " BACK");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bl, COL_TEXT, 0);
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "SEND PAYMENT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* Channel info band */
    lv_obj_t *ch_bar = lv_obj_create(scr);
    lv_obj_set_pos(ch_bar, 0, 44); lv_obj_set_size(ch_bar, 800, 36);
    lv_obj_set_style_bg_color(ch_bar, lv_color_hex(0x1a202e), 0);
    lv_obj_set_style_border_width(ch_bar, 0, 0);
    lv_obj_clear_flag(ch_bar, LV_OBJ_FLAG_SCROLLABLE);
    lbl_channel_info = lv_label_create(ch_bar);
    lv_label_set_text(lbl_channel_info, "No channel selected — go back and pick one");
    lv_obj_set_style_text_font(lbl_channel_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_channel_info, COL_MUTED, 0);
    lv_obj_align(lbl_channel_info, LV_ALIGN_LEFT_MID, 12, 0);

    /* Left: numpad (for manual CKB amount if no invoice) */
    lv_obj_t *numpad_cont = lv_obj_create(scr);
    lv_obj_set_pos(numpad_cont, 12, 90); lv_obj_set_size(numpad_cont, 350, 340);
    lv_obj_set_style_bg_color(numpad_cont, COL_SURFACE, 0);
    lv_obj_set_style_radius(numpad_cont, 10, 0);
    lv_obj_set_style_border_color(numpad_cont, COL_BORDER, 0);
    lv_obj_set_style_border_width(numpad_cont, 1, 0);
    lv_obj_clear_flag(numpad_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Simple numpad using button matrix */
    static const char *numpad_map[] = {
        "7", "8", "9", "\n",
        "4", "5", "6", "\n",
        "1", "2", "3", "\n",
        ".", "0", LV_SYMBOL_BACKSPACE, ""
    };
    lv_obj_t *btnm = lv_btnmatrix_create(numpad_cont);
    lv_btnmatrix_set_map(btnm, numpad_map);
    lv_obj_set_size(btnm, 326, 312);
    lv_obj_align(btnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(btnm, COL_SURFACE, 0);
    lv_obj_set_style_bg_color(btnm, COL_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btnm, COL_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnm, 1, LV_PART_ITEMS);
    lv_obj_set_style_text_font(btnm, &lv_font_montserrat_24, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, COL_TEXT, LV_PART_ITEMS);

    /* Right: invoice entry + details */
    lv_obj_t *right = lv_obj_create(scr);
    lv_obj_set_pos(right, 374, 90); lv_obj_set_size(right, 414, 340);
    lv_obj_set_style_bg_color(right, COL_SURFACE, 0);
    lv_obj_set_style_radius(right, 10, 0);
    lv_obj_set_style_border_color(right, COL_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_pad_all(right, 16, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_inv_t = lv_label_create(right);
    lv_label_set_text(lbl_inv_t, "PASTE INVOICE");
    lv_obj_set_style_text_font(lbl_inv_t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_inv_t, COL_MUTED, 0);
    lv_obj_align(lbl_inv_t, LV_ALIGN_TOP_LEFT, 0, 0);

    ta_invoice = lv_textarea_create(right);
    lv_obj_set_size(ta_invoice, 382, 90);
    lv_obj_align(ta_invoice, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_textarea_set_placeholder_text(ta_invoice, "ckbinv1...");
    lv_textarea_set_one_line(ta_invoice, false);
    lv_obj_set_style_bg_color(ta_invoice, lv_color_hex(0x0d0f14), 0);
    lv_obj_set_style_border_color(ta_invoice, COL_BORDER, 0);
    lv_obj_set_style_text_font(ta_invoice, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ta_invoice, COL_ACCENT2, 0);
    lv_obj_add_event_cb(ta_invoice, on_invoice_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_amount = lv_label_create(right);
    lv_label_set_text(lbl_amount, "Amount: —");
    lv_obj_set_style_text_font(lbl_amount, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_amount, COL_WARN, 0);
    lv_obj_align(lbl_amount, LV_ALIGN_TOP_LEFT, 0, 124);

    lbl_fee = lv_label_create(right);
    lv_label_set_text(lbl_fee, "Max fee: 0.1 CKB");
    lv_obj_set_style_text_font(lbl_fee, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_fee, COL_MUTED, 0);
    lv_obj_align(lbl_fee, LV_ALIGN_TOP_LEFT, 0, 154);

    lbl_err = lv_label_create(right);
    lv_label_set_text(lbl_err, "");
    lv_obj_set_style_text_font(lbl_err, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_err, COL_DANGER, 0);
    lv_obj_align(lbl_err, LV_ALIGN_TOP_LEFT, 0, 180);

    /* Send button */
    lv_obj_t *btn_send = lv_btn_create(right);
    lv_obj_set_size(btn_send, 382, 60);
    lv_obj_align(btn_send, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_send, COL_ACCENT, 0);
    lv_obj_set_style_bg_color(btn_send, lv_color_darken(COL_ACCENT, 40), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_send, 10, 0);
    lv_obj_add_event_cb(btn_send, on_send_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_t *send_lbl = lv_label_create(btn_send);
    lv_label_set_text(send_lbl, "SEND " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(send_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(send_lbl, COL_BG, 0);
    lv_obj_center(send_lbl);

    return scr;
}

void send_refresh(const FkState *s) {
    if (!scr) return;
    int idx = s->selected_channel_idx;
    if (idx >= 0 && idx < s->channel_len) {
        const FkChannel *ch = &s->channels[idx];
        char info[128];
        snprintf(info, sizeof(info), "Via: %.10s...  Local: %.2f CKB  Capacity: %.2f CKB",
                 ch->peer_id,
                 (double)ch->local_balance_ckb / 1e8,
                 (double)ch->capacity_ckb / 1e8);
        lv_label_set_text(lbl_channel_info, info);
    } else {
        lv_label_set_text(lbl_channel_info, "No channel selected");
    }
}
