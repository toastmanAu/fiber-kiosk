/*
 * receive.c — Receive payment screen (generate invoice + QR)
 *
 *  ┌─────────────────────────────────────────────────────────────┐
 *  │ ← BACK                   RECEIVE                           │
 *  ├──────────────────────┬──────────────────────────────────────┤
 *  │                      │  Amount: [   1000   ] CKB            │
 *  │    ████████████      │  Description: optional               │
 *  │    ██ QR CODE ██      │                                      │
 *  │    ████████████      │  [ GENERATE INVOICE ]                │
 *  │                      │                                      │
 *  │  ckbinv1...          │  Status: Waiting for payment...      │
 *  │  (truncated)         │                                      │
 *  │                      │  [ COPY ] (to clipboard/serial)     │
 *  └──────────────────────┴──────────────────────────────────────┘
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
static lv_obj_t *qr;
static lv_obj_t *lbl_invoice_short;
static lv_obj_t *lbl_status;
static lv_obj_t *ta_amount;

static void on_back(lv_event_t *e) { (void)e; fk_nav_to(SCREEN_HOME); }

static void on_generate(lv_event_t *e) {
    (void)e;
    const char *amt_str = lv_textarea_get_text(ta_amount);
    double ckb = atof(amt_str && *amt_str ? amt_str : "0");
    int64_t shannons = (int64_t)(ckb * 1e8);

    char invoice[512] = {0};
    char payment_hash[67] = {0};
    char err[256] = {0};

    lv_label_set_text(lbl_status, "Generating invoice...");

    int rc = fk_bridge_new_invoice(shannons, "Fiber Kiosk", 600,
                                    invoice, payment_hash, err, sizeof(err));
    if (rc < 0) {
        lv_label_set_text_fmt(lbl_status, "Error: %s", err);
        return;
    }

    /* Save to global state */
    pthread_mutex_lock(&g_state_mutex);
    strncpy(g_state.active_invoice, invoice, sizeof(g_state.active_invoice)-1);
    strncpy(g_state.active_payment_hash, payment_hash, sizeof(g_state.active_payment_hash)-1);
    g_state.active_invoice_amount = shannons;
    pthread_mutex_unlock(&g_state_mutex);

    /* Update QR */
    lv_qrcode_update(qr, invoice, strlen(invoice));

    /* Truncate invoice for display */
    char short_inv[48];
    snprintf(short_inv, sizeof(short_inv), "%.22s...%.8s", invoice, invoice + strlen(invoice) - 8);
    lv_label_set_text(lbl_invoice_short, short_inv);

    lv_label_set_text(lbl_status, LV_SYMBOL_REFRESH " Waiting for payment...");
    lv_obj_set_style_text_color(lbl_status, COL_WARN, 0);
}

lv_obj_t *receive_build(void) {
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
    lv_label_set_text(title, "RECEIVE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* Left panel — QR */
    lv_obj_t *left = lv_obj_create(scr);
    lv_obj_set_pos(left, 12, 56); lv_obj_set_size(left, 370, 410);
    lv_obj_set_style_bg_color(left, COL_SURFACE, 0);
    lv_obj_set_style_radius(left, 10, 0);
    lv_obj_set_style_border_color(left, COL_BORDER, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    qr = lv_qrcode_create(left);
    lv_qrcode_set_size(qr, 300);
    lv_qrcode_set_dark_color(qr, COL_TEXT);
    lv_qrcode_set_light_color(qr, COL_BG);
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 12);
    lv_qrcode_update(qr, "fiber-kiosk:ready", 17); /* placeholder */

    lbl_invoice_short = lv_label_create(left);
    lv_label_set_text(lbl_invoice_short, "— generate invoice to show QR —");
    lv_obj_set_style_text_font(lbl_invoice_short, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_invoice_short, COL_MUTED, 0);
    lv_label_set_long_mode(lbl_invoice_short, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_invoice_short, 346);
    lv_obj_align(lbl_invoice_short, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* Right panel — controls */
    lv_obj_t *right = lv_obj_create(scr);
    lv_obj_set_pos(right, 394, 56); lv_obj_set_size(right, 394, 410);
    lv_obj_set_style_bg_color(right, COL_SURFACE, 0);
    lv_obj_set_style_radius(right, 10, 0);
    lv_obj_set_style_border_color(right, COL_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_pad_all(right, 20, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *amt_title = lv_label_create(right);
    lv_label_set_text(amt_title, "AMOUNT (CKB)");
    lv_obj_set_style_text_font(amt_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(amt_title, COL_MUTED, 0);
    lv_obj_align(amt_title, LV_ALIGN_TOP_LEFT, 0, 0);

    ta_amount = lv_textarea_create(right);
    lv_obj_set_size(ta_amount, 354, 52);
    lv_obj_align(ta_amount, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_textarea_set_placeholder_text(ta_amount, "100.0");
    lv_textarea_set_one_line(ta_amount, true);
    lv_obj_set_style_bg_color(ta_amount, COL_BG, 0);
    lv_obj_set_style_border_color(ta_amount, COL_BORDER, 0);
    lv_obj_set_style_text_font(ta_amount, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ta_amount, COL_WARN, 0);

    lv_obj_t *btn_gen = lv_btn_create(right);
    lv_obj_set_size(btn_gen, 354, 56);
    lv_obj_align(btn_gen, LV_ALIGN_TOP_LEFT, 0, 90);
    lv_obj_set_style_bg_color(btn_gen, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_gen, 10, 0);
    lv_obj_add_event_cb(btn_gen, on_generate, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gen_lbl = lv_label_create(btn_gen);
    lv_label_set_text(gen_lbl, LV_SYMBOL_REFRESH " GENERATE INVOICE");
    lv_obj_set_style_text_font(gen_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(gen_lbl, COL_BG, 0);
    lv_obj_center(gen_lbl);

    lbl_status = lv_label_create(right);
    lv_label_set_text(lbl_status, "Enter amount and generate invoice");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_status, COL_MUTED, 0);
    lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_status, 354);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_LEFT, 0, 162);

    return scr;
}

/* Check if active invoice got paid (called from refresh cycle) */
void receive_refresh(const FkState *s) {
    if (!scr) return;
    if (!s->active_payment_hash[0]) return;

    /* Poll invoice status */
    int status = fk_bridge_check_invoice(s->active_payment_hash);
    if (status == 1) {
        lv_label_set_text(lbl_status, LV_SYMBOL_OK " PAYMENT RECEIVED!");
        lv_obj_set_style_text_color(lbl_status, COL_ACCENT, 0);
        /* Clear active invoice */
        pthread_mutex_lock(&g_state_mutex);
        g_state.active_payment_hash[0] = '\0';
        g_state.active_invoice[0] = '\0';
        pthread_mutex_unlock(&g_state_mutex);
    } else if (status == -1) {
        lv_label_set_text(lbl_status, "Invoice expired");
        lv_obj_set_style_text_color(lbl_status, COL_DANGER, 0);
    }
}
