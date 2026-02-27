/*
 * channels.c — Channel list screen
 *
 *  ┌──────────────────────────────────────────────────────────────┐
 *  │ ← BACK       CHANNELS (3 open)              [+ OPEN]        │
 *  ├──────────────────────────────────────────────────────────────┤
 *  │  Peer          Local        Remote      Capacity   State    │
 *  ├──────────────────────────────────────────────────────────────┤
 *  │  026a9d...     9,500 CKB    500 CKB     10,000     OPEN     │
 *  │  0301ae...     250 CKB      4,750 CKB    5,000     OPEN     │
 *  │  03ab12...     0 CKB        2,000 CKB    2,000     CLOSING  │
 *  ├──────────────────────────────────────────────────────────────┤
 *  │  Tap a channel to select it for sending                     │
 *  └──────────────────────────────────────────────────────────────┘
 */

#include "screens.h"
#include "../core/state.h"
#include "../core/bridge.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>

#define COL_BG       lv_color_hex(0x0d0f14)
#define COL_SURFACE  lv_color_hex(0x161a22)
#define COL_ACCENT   lv_color_hex(0x3bc67a)
#define COL_ACCENT2  lv_color_hex(0x4a9eff)
#define COL_WARN     lv_color_hex(0xf5a623)
#define COL_DANGER   lv_color_hex(0xe74c3c)
#define COL_TEXT     lv_color_hex(0xe2e8f0)
#define COL_MUTED    lv_color_hex(0x64748b)
#define COL_BORDER   lv_color_hex(0x252b38)

static lv_obj_t *scr;
static lv_obj_t *lbl_count;
static lv_obj_t *table;

static const char *state_str(ChanState s) {
    switch (s) {
        case CHAN_OPEN:    return "OPEN";
        case CHAN_PENDING: return "PENDING";
        case CHAN_CLOSING: return "CLOSING";
        case CHAN_CLOSED:  return "CLOSED";
        default:           return "?";
    }
}

/* Row tap — select channel and go to send */
static void on_row_tap(lv_event_t *e) {
    uint32_t row, col;
    lv_table_get_selected_cell(table, &row, &col);
    if (row == 0) return; /* header row */

    int idx = (int)row - 1;
    pthread_mutex_lock(&g_state_mutex);
    if (idx < g_state.channel_len)
        g_state.selected_channel_idx = idx;
    pthread_mutex_unlock(&g_state_mutex);

    fk_nav_to(SCREEN_SEND);
}

static void on_back(lv_event_t *e)  { (void)e; fk_nav_to(SCREEN_HOME); }

lv_obj_t *channels_build(void) {
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Header bar */
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
    lv_obj_t *back_lbl = lv_label_create(btn_back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " BACK");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(back_lbl, COL_TEXT, 0);
    lv_obj_center(back_lbl);

    lbl_count = lv_label_create(bar);
    lv_label_set_text(lbl_count, "CHANNELS");
    lv_obj_set_style_text_font(lbl_count, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_count, COL_TEXT, 0);
    lv_obj_align(lbl_count, LV_ALIGN_CENTER, 0, 0);

    /* Table */
    table = lv_table_create(scr);
    lv_obj_set_pos(table, 0, 44);
    lv_obj_set_size(table, 800, 390);
    lv_obj_set_style_bg_color(table, COL_BG, 0);
    lv_obj_set_style_border_width(table, 0, 0);

    /* Column widths: Peer | Local | Remote | Capacity | State */
    lv_table_set_column_count(table, 5);
    lv_table_set_column_width(table, 0, 220);
    lv_table_set_column_width(table, 1, 160);
    lv_table_set_column_width(table, 2, 160);
    lv_table_set_column_width(table, 3, 130);
    lv_table_set_column_width(table, 4, 110);

    /* Header row */
    lv_table_set_cell_value(table, 0, 0, "PEER");
    lv_table_set_cell_value(table, 0, 1, "LOCAL");
    lv_table_set_cell_value(table, 0, 2, "REMOTE");
    lv_table_set_cell_value(table, 0, 3, "CAPACITY");
    lv_table_set_cell_value(table, 0, 4, "STATE");

    lv_obj_add_event_cb(table, on_row_tap, LV_EVENT_VALUE_CHANGED, NULL);

    /* Hint */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Tap a channel to send via it");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, COL_MUTED, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

void channels_refresh(const FkState *s) {
    if (!scr || !table) return;

    lv_label_set_text_fmt(lbl_count, "CHANNELS  (%d open)", s->channel_count);

    /* Update table rows (skip header row 0) */
    for (int i = 0; i < s->channel_len && i < FK_MAX_CHANNELS; i++) {
        const FkChannel *ch = &s->channels[i];
        int row = i + 1;

        /* Peer ID — truncated */
        char peer_short[24];
        snprintf(peer_short, sizeof(peer_short), "%.8s...%.4s",
                 ch->peer_id, ch->peer_id + strlen(ch->peer_id) - 4);
        lv_table_set_cell_value(table, row, 0, peer_short);

        char lbal[32], rbal[32], cap[32];
        snprintf(lbal, sizeof(lbal), "%.2f CKB", (double)ch->local_balance_ckb / 1e8);
        snprintf(rbal, sizeof(rbal), "%.2f CKB", (double)ch->remote_balance_ckb / 1e8);
        snprintf(cap,  sizeof(cap),  "%.0f",     (double)ch->capacity_ckb / 1e8);

        lv_table_set_cell_value(table, row, 1, lbal);
        lv_table_set_cell_value(table, row, 2, rbal);
        lv_table_set_cell_value(table, row, 3, cap);
        lv_table_set_cell_value(table, row, 4, state_str(ch->state));

        /* Highlight selected */
        if (i == s->selected_channel_idx) {
            lv_obj_set_style_bg_color(table, COL_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
        }
    }
}
