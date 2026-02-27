/*
 * pin.c — PIN lock screen
 *
 * Shown on startup (and after pin_timeout_sec of inactivity) when
 * the signer is connected but locked. Also shown when signer first
 * connects (to send the unlock command).
 *
 * Layout (800×480):
 *   ┌─────────────────────────────────────────┐
 *   │         FIBER NODE  [status dot]        │
 *   │                                         │
 *   │         ● ● ● ●  (PIN dots)             │
 *   │         [error label — hidden]          │
 *   │                                         │
 *   │    [1] [2] [3]                          │
 *   │    [4] [5] [6]                          │
 *   │    [7] [8] [9]                          │
 *   │    [←] [0] [✓]                          │
 *   └─────────────────────────────────────────┘
 *
 * PIN is sent to signer via fiber-bridge POST /signer/unlock
 * On success → navigate to SCREEN_HOME
 * On 5 failures → signer wipes keys (hardware enforced)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "lvgl/lvgl.h"
#include "../core/state.h"
#include "../core/bridge.h"
#include "../core/config.h"
#include "screens.h"

/* ── Constants ──────────────────────────────────────────────────── */
#define PIN_MAX_LEN     8
#define PIN_MIN_LEN     4

/* Colours */
#define COL_BG          0x0D1117   /* near-black */
#define COL_SURFACE     0x161B22   /* card surface */
#define COL_ACCENT      0x00D4AA   /* CKB teal */
#define COL_ACCENT_DIM  0x007A61
#define COL_TEXT        0xE6EDF3
#define COL_MUTED       0x484F58
#define COL_ERROR       0xF85149
#define COL_DOT_FILLED  0x00D4AA
#define COL_DOT_EMPTY   0x30363D

/* Button size */
#define BTN_W   160
#define BTN_H    72
#define BTN_GAP  12

/* ── State ──────────────────────────────────────────────────────── */
static char      s_pin[PIN_MAX_LEN + 1];
static int       s_pin_len = 0;
static int       s_attempts = 0;

/* LVGL objects we need to update */
static lv_obj_t *s_dots[PIN_MAX_LEN];
static lv_obj_t *s_error_label;
static lv_obj_t *s_status_dot;
static lv_obj_t *s_status_label;
static lv_obj_t *s_confirm_btn;
static lv_obj_t *s_screen;

/* ── Helpers ─────────────────────────────────────────────────────── */
static void pin_update_dots(void) {
    for (int i = 0; i < PIN_MAX_LEN; i++) {
        lv_obj_t *dot = s_dots[i];
        if (dot == NULL) continue;
        lv_color_t col = (i < s_pin_len)
            ? lv_color_hex(COL_DOT_FILLED)
            : lv_color_hex(COL_DOT_EMPTY);
        lv_obj_set_style_bg_color(dot, col, 0);
    }
    /* Enable confirm only when PIN >= MIN_LEN */
    if (s_confirm_btn) {
        if (s_pin_len >= PIN_MIN_LEN)
            lv_obj_clear_state(s_confirm_btn, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_confirm_btn, LV_STATE_DISABLED);
    }
}

static void pin_show_error(const char *msg) {
    if (!s_error_label) return;
    lv_label_set_text(s_error_label, msg);
    lv_obj_clear_flag(s_error_label, LV_OBJ_FLAG_HIDDEN);
}

static void pin_clear_error(void) {
    if (!s_error_label) return;
    lv_obj_add_flag(s_error_label, LV_OBJ_FLAG_HIDDEN);
}

static void pin_reset(void) {
    s_pin_len = 0;
    memset(s_pin, 0, sizeof(s_pin));
    pin_update_dots();
    pin_clear_error();
}

/* ── Submit PIN ──────────────────────────────────────────────────── */
static void pin_submit(void) {
    if (s_pin_len < PIN_MIN_LEN) return;

    char err[256] = {0};
    int result = fk_bridge_signer_unlock(s_pin, err, sizeof(err));

    if (result == 0) {
        /* Success — update state + navigate home */
        pthread_mutex_lock(&g_state_mutex);
        g_state.signer_unlocked = true;
        pthread_mutex_unlock(&g_state_mutex);

        s_attempts = 0;
        pin_reset();
        fk_nav_to(SCREEN_HOME);
    } else if (result == -2) {
        /* Signer reports wipe (5 attempts exceeded) */
        s_attempts = 5;
        pin_show_error("⚠  KEYS WIPED — too many attempts");
        pin_reset();
    } else {
        s_attempts++;
        int remaining = 5 - s_attempts;
        if (remaining <= 0) {
            pin_show_error("⚠  KEYS WIPED — too many attempts");
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "Incorrect PIN — %d attempt%s remaining",
                     remaining, remaining == 1 ? "" : "s");
            pin_show_error(msg);
        }
        pin_reset();
    }
}

/* ── Button event handler ────────────────────────────────────────── */
static void pin_skip_cb(lv_event_t *e) {
    (void)e;
    fk_nav_to(SCREEN_HOME);
}

static void pin_btn_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    const char *label = (const char *)lv_event_get_user_data(e);

    if (!label) return;

    if (label[0] == '<') {
        /* Backspace */
        if (s_pin_len > 0) {
            s_pin_len--;
            s_pin[s_pin_len] = '\0';
            pin_update_dots();
            pin_clear_error();
        }
    } else if (label[0] == 'C') {
        /* Confirm */
        pin_submit();
    } else if (label[0] >= '0' && label[0] <= '9') {
        /* Digit */
        if (s_pin_len < PIN_MAX_LEN) {
            s_pin[s_pin_len++] = label[0];
            s_pin[s_pin_len]   = '\0';
            pin_update_dots();
            pin_clear_error();
        }
    }
    (void)btn;
}

/* ── Build a keypad button ───────────────────────────────────────── */
static lv_obj_t *make_key(lv_obj_t *parent, const char *digit,
                           const char *user_data_str, bool is_confirm) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, BTN_W, BTN_H);

    if (is_confirm) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT_DIM),
                                  LV_STATE_DISABLED);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_SURFACE), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0D1117),
                                  LV_STATE_PRESSED);
    }
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, digit);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_center(lbl);

    /* Pass the key string as user_data */
    lv_obj_add_event_cb(btn, pin_btn_cb, LV_EVENT_CLICKED,
                        (void *)user_data_str);

    return btn;
}

/* ── Build screen ────────────────────────────────────────────────── */
lv_obj_t *pin_build(void) {
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_size(s_screen, 800, 480);

    /* ── Header ── */
    lv_obj_t *header = lv_obj_create(s_screen);
    lv_obj_set_size(header, 800, 56);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "FIBER NODE");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 20, 0);

    /* Status dot in header */
    s_status_dot = lv_obj_create(header);
    lv_obj_set_size(s_status_dot, 10, 10);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(s_status_dot, LV_ALIGN_RIGHT_MID, -60, 0);

    s_status_label = lv_label_create(header);
    lv_label_set_text(s_status_label, "Signer locked");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_label, LV_ALIGN_RIGHT_MID, -12, 0);

    /* ── Left panel: PIN display ── */
    lv_obj_t *left = lv_obj_create(s_screen);
    lv_obj_set_size(left, 340, 424);
    lv_obj_align(left, LV_ALIGN_TOP_LEFT, 0, 56);
    lv_obj_set_style_bg_color(left, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *unlock_lbl = lv_label_create(left);
    lv_label_set_text(unlock_lbl, "Enter PIN to unlock signer");
    lv_obj_set_style_text_color(unlock_lbl, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(unlock_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(unlock_lbl, LV_ALIGN_TOP_MID, 0, 40);

    /* PIN dots row */
    lv_obj_t *dots_cont = lv_obj_create(left);
    lv_obj_set_size(dots_cont, 280, 40);
    lv_obj_align(dots_cont, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(dots_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots_cont, 0, 0);
    lv_obj_clear_flag(dots_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(dots_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_cont, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots_cont, 16, 0);

    for (int i = 0; i < PIN_MAX_LEN; i++) {
        lv_obj_t *dot = lv_obj_create(dots_cont);
        lv_obj_set_size(dot, 18, 18);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(COL_DOT_EMPTY), 0);
        s_dots[i] = dot;
    }

    /* Error label */
    s_error_label = lv_label_create(left);
    lv_label_set_text(s_error_label, "");
    lv_obj_set_style_text_color(s_error_label, lv_color_hex(COL_ERROR), 0);
    lv_obj_set_style_text_font(s_error_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_error_label, 300);
    lv_label_set_long_mode(s_error_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_error_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_error_label, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_add_flag(s_error_label, LV_OBJ_FLAG_HIDDEN);

    /* "No signer" hint when not connected */
    lv_obj_t *hint = lv_label_create(left);
    lv_label_set_text(hint,
        "Connect ESP32-P4 signer\nto /dev/ttyACM0\nthen restart bridge");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* Skip button (run in degraded mode without signer) */
    lv_obj_t *skip_btn = lv_btn_create(left);
    lv_obj_set_size(skip_btn, 180, 44);
    lv_obj_align(skip_btn, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_bg_color(skip_btn, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_radius(skip_btn, 8, 0);
    lv_obj_set_style_border_width(skip_btn, 1, 0);
    lv_obj_set_style_border_color(skip_btn, lv_color_hex(COL_MUTED), 0);

    lv_obj_t *skip_lbl = lv_label_create(skip_btn);
    lv_label_set_text(skip_lbl, "Continue without signer");
    lv_obj_set_style_text_color(skip_lbl, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(skip_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(skip_lbl);

    lv_obj_add_event_cb(skip_btn, pin_skip_cb, LV_EVENT_CLICKED, NULL);

    /* ── Right panel: Keypad ── */
    lv_obj_t *right = lv_obj_create(s_screen);
    lv_obj_set_size(right, 460, 424);
    lv_obj_align(right, LV_ALIGN_TOP_RIGHT, 0, 56);
    lv_obj_set_style_bg_color(right, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    /* Grid: 3 cols × 4 rows */
    static lv_coord_t col_dsc[] = { BTN_W, BTN_W, BTN_W, LV_GRID_TEMPLATE_LAST };
    static lv_coord_t row_dsc[] = { BTN_H, BTN_H, BTN_H, BTN_H, LV_GRID_TEMPLATE_LAST };

    lv_obj_t *grid = lv_obj_create(right);
    lv_obj_set_size(grid, 3 * BTN_W + 2 * BTN_GAP + 8,
                          4 * BTN_H + 3 * BTN_GAP + 8);
    lv_obj_center(grid);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, BTN_GAP, 0);
    lv_obj_set_style_pad_row(grid, BTN_GAP, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    /* Key labels and user_data strings (static so they outlive this function) */
    static const char *keys[4][3] = {
        { "1", "2", "3" },
        { "4", "5", "6" },
        { "7", "8", "9" },
        { "<", "0", "C" },  /* < = backspace, C = confirm */
    };
    static const char *key_display[4][3] = {
        { "1", "2", "3" },
        { "4", "5", "6" },
        { "7", "8", "9" },
        { LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK },
    };

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            bool is_confirm = (keys[row][col][0] == 'C');
            lv_obj_t *btn = make_key(grid, key_display[row][col],
                                     keys[row][col], is_confirm);
            lv_obj_set_grid_cell(btn,
                LV_GRID_ALIGN_STRETCH, col, 1,
                LV_GRID_ALIGN_STRETCH, row, 1);
            if (is_confirm) {
                s_confirm_btn = btn;
                lv_obj_add_state(btn, LV_STATE_DISABLED); /* disabled until PIN_MIN_LEN */
            }
        }
    }

    return s_screen;
}

/* ── Refresh (called from main loop) ────────────────────────────── */
void pin_refresh(const FkState *s) {
    if (!s_screen) return;

    if (s->signer_connected && !s->signer_unlocked) {
        /* Signer present but locked — normal state for this screen */
        lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(0xF0A500), 0);
        lv_label_set_text(s_status_label, "Signer locked");
    } else if (!s->signer_connected) {
        /* No signer — show degraded mode info */
        lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(COL_MUTED), 0);
        lv_label_set_text(s_status_label, "No signer");
    } else if (s->signer_unlocked) {
        /* Already unlocked — shouldn't be on this screen, nav away */
        lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(COL_ACCENT), 0);
        lv_label_set_text(s_status_label, "Signer unlocked");
        fk_nav_to(SCREEN_HOME);
    }
}
