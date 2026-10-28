#include "ui.h"
#include "ui_statusbar.h"
#include "../gateway/bt_gateway.h"

/* Geometria e cores. O preview le estes #define por regex (ui_parse.py), entao
 * mantenha o formato "#define NOME valor".
 *
 * So o icone de Bluetooth, no canto superior direito de todas as telas, e so
 * enquanto o ELM327 estiver conectado. Sem fundo: o topo das telas e preto (a
 * faixa em gradiente saiu do nanomode). Margem 0: a tinta do simbolo em
 * Montserrat 12 vai ate a linha 13, e os digitos do gauge superior direito do
 * trackmode, em Montserrat 28, comecam na 15 -- medido nos glifos do LVGL. */
#define STATUSBAR_ALIGN       LV_ALIGN_TOP_RIGHT
#define STATUSBAR_MARGIN_X    4      /* distancia da borda direita */
#define STATUSBAR_MARGIN_Y    0      /* distancia do topo; 0 para parar antes da linha 15 */
#define STATUSBAR_REFRESH_MS  500

#define STATUSBAR_COLOR_BT    0x4AA3FF

static lv_obj_t *bt_lbl;
static lv_timer_t *timer;

static void refresh(void)
{
    bool show = bt_is_connected();
#if UI_ENABLE_SPLASH
    if (ui_splash && lv_screen_active() == ui_splash) show = false;   /* no splash fica fora */
#endif
    if (show) lv_obj_remove_flag(bt_lbl, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(bt_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh();
}

void ui_statusbar_init(void)
{
    /* lv_layer_top() ja nasce nao-clicavel; o label tambem nao pode capturar
     * toque, senao o swipe morre embaixo dele. */
    bt_lbl = lv_label_create(lv_layer_top());
    lv_label_set_text(bt_lbl, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(bt_lbl, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bt_lbl, lv_color_hex(STATUSBAR_COLOR_BT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(bt_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(bt_lbl, STATUSBAR_ALIGN, -STATUSBAR_MARGIN_X, STATUSBAR_MARGIN_Y);

    refresh();
    timer = lv_timer_create(timer_cb, STATUSBAR_REFRESH_MS, NULL);
}
