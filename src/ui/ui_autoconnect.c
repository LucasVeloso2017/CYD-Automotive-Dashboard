#include "ui.h"
#include "ui_autoconnect.h"
#include "../gateway/bt_gateway.h"

/* Teto do loading: passado isso o usuario nao espera mais, achando ou nao. A
 * busca Bluetooth continua por tras para a lista (12,8 s). */
#define AUTOCONNECT_WINDOW_MS   2000
#define AUTOCONNECT_TICK_MS     50

#define AC_COLOR_BG             0x000000
#define AC_COLOR_TEXT           0xFFFFFF
#define AC_COLOR_MUTED          0x8B96A5
#define AC_COLOR_ACCENT         0xFF9B00

static lv_obj_t   *overlay;
static lv_obj_t   *status_lbl;
static lv_timer_t *timer;
static uint32_t    started_ms;
static bool        shown_connecting;

static void close_overlay(void)
{
    if (timer) {
        lv_timer_delete(timer);
        timer = NULL;
    }
    if (overlay) {
        lv_obj_delete(overlay);
        overlay = status_lbl = NULL;
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    const uint32_t elapsed = lv_tick_elaps(started_ms);

    /* Conectou (ou ja estava conectado): nada mais a esperar. */
    if (bt_is_connected() || elapsed >= AUTOCONNECT_WINDOW_MS) {
        close_overlay();
        return;
    }
    if (bt_autoconnect_found() && !shown_connecting) {
        shown_connecting = true;
        lv_label_set_text(status_lbl, "Conectando ao OBDII...");
        return;
    }
    /* Busca encerrada sem dongle: nao faz o usuario esperar o resto da janela. */
    if (!bt_autoconnect_active() && !bt_autoconnect_found() && !bt_is_connecting()) {
        close_overlay();
    }
}

void ui_autoconnect_init(void)
{
    bt_autoconnect_start(AUTOCONNECT_WINDOW_MS);
    if (!bt_autoconnect_active()) return;      /* Bluetooth fora do ar ou ja conectado */

    /* Camada de cima, cobrindo a tela e o toque: nada de swipe por baixo
     * durante os 2 s. */
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(AC_COLOR_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sp = lv_spinner_create(overlay);
    lv_obj_set_size(sp, 54, 54);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, -18);
    lv_obj_set_style_arc_color(sp, lv_color_hex(AC_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(sp, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(sp, LV_OBJ_FLAG_CLICKABLE);

    status_lbl = lv_label_create(overlay);
    lv_label_set_text(status_lbl, "Procurando OBDII...");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(AC_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 34);

    started_ms = lv_tick_get();
    shown_connecting = false;
    timer = lv_timer_create(tick_cb, AUTOCONNECT_TICK_MS, NULL);
}
