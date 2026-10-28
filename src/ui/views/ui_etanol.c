#include "../ui.h"

#include <string.h>

/* Percentual de etanol no combustivel (PID 0x52), logo depois do trackmode.
 *
 * Numero grande, barra de 0 a 100 e a classificacao do que esta no tanque.
 * Textos em ASCII: as fontes Montserrat do LVGL nao tem acentos. Mantenha os
 * "#define ETH_NOME valor" neste formato. */
#define ETH_TITLE_Y           18
#define ETH_VALUE_Y           (-12)
#define ETH_BAR_W             280
#define ETH_BAR_H             16
#define ETH_BAR_Y             52
#define ETH_SCALE_Y           74
#define ETH_CLASS_Y           100

/* Faixas de classificacao, em % de etanol. A gasolina comum no Brasil e E30
 * desde ago/2025 (lida 29 % no F20); etanol hidratado fica acima de 90. */
#define ETH_GASOLINA_MAX      35
#define ETH_ETANOL_MIN        85

#define ETH_COLOR_TEXT        0xFFFFFF
#define ETH_COLOR_MUTED       0x8B96A5
#define ETH_COLOR_ACCENT      0xFF9B00
#define ETH_COLOR_TRACK       0x1C1C1C

lv_obj_t *ui_etanol = NULL;

static lv_obj_t *value_lbl, *unit_lbl, *bar, *class_lbl;

/* O que esta desenhado, para so tocar no LVGL quando muda (o tick e de 16 ms). */
static int  drawn_pct = -2;
static bool drawn_connected = true;

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    return l;
}

static const char *classify(int pct)
{
    if (pct <= ETH_GASOLINA_MAX) return "GASOLINA";
    if (pct >= ETH_ETANOL_MIN) return "ETANOL";
    return "MISTURA";
}

void ui_etanol_screen_init(void)
{
    ui_etanol = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_etanol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_etanol, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_etanol, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = text(ui_etanol, "PERCENTUAL DE ETANOL", &lv_font_montserrat_16, ETH_COLOR_MUTED);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, ETH_TITLE_Y);

    /* Numero e "%" lado a lado, centrados juntos: o conjunto muda de largura
     * entre "5" e "100", entao vai num container com flex. */
    lv_obj_t *row = lv_obj_create(ui_etanol);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, ETH_VALUE_Y);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    value_lbl = text(row, "--", &lv_font_montserrat_48, ETH_COLOR_TEXT);
    unit_lbl  = text(row, "%", &lv_font_montserrat_28, ETH_COLOR_ACCENT);
    lv_obj_set_style_pad_bottom(unit_lbl, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

    bar = lv_bar_create(ui_etanol);
    lv_obj_set_size(bar, ETH_BAR_W, ETH_BAR_H);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, ETH_BAR_Y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(ETH_COLOR_TRACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, ETH_BAR_H / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(ETH_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, ETH_BAR_H / 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    /* Escala nas pontas e no meio da barra. */
    static const struct { const char *t; lv_align_t a; } SCALE[] = {
        { "0",   LV_ALIGN_OUT_BOTTOM_LEFT  },
        { "50",  LV_ALIGN_OUT_BOTTOM_MID   },
        { "100", LV_ALIGN_OUT_BOTTOM_RIGHT },
    };
    for (unsigned i = 0; i < sizeof(SCALE) / sizeof(SCALE[0]); i++) {
        lv_obj_t *l = text(ui_etanol, SCALE[i].t, &lv_font_montserrat_12, ETH_COLOR_MUTED);
        lv_obj_align_to(l, bar, SCALE[i].a, 0, ETH_SCALE_Y - ETH_BAR_Y - ETH_BAR_H);
    }

    class_lbl = text(ui_etanol, "SEM CONEXAO", &lv_font_montserrat_16, ETH_COLOR_MUTED);
    lv_obj_align(class_lbl, LV_ALIGN_CENTER, 0, ETH_CLASS_Y);

    drawn_pct = -2;
    drawn_connected = true;
    ui_etanol_render(-1.0f, false);
}

void ui_etanol_render(float pct, bool connected)
{
    if (!ui_etanol) return;

    /* Inteiro arredondado: a resolucao do PID e 0,4 %, casa decimal seria ruido. */
    const int v = (connected && pct >= 0.0f) ? (int)(pct + 0.5f) : -1;
    if (v == drawn_pct && connected == drawn_connected) return;
    drawn_pct = v;
    drawn_connected = connected;

    if (v < 0) {
        lv_label_set_text(value_lbl, "--");
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_label_set_text(class_lbl, connected ? "AGUARDANDO A DME" : "SEM CONEXAO");
        lv_obj_set_style_text_color(class_lbl, lv_color_hex(ETH_COLOR_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }

    lv_label_set_text_fmt(value_lbl, "%d", v > 100 ? 100 : v);
    lv_bar_set_value(bar, v > 100 ? 100 : v, LV_ANIM_OFF);
    lv_label_set_text(class_lbl, classify(v));
    lv_obj_set_style_text_color(class_lbl, lv_color_hex(ETH_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_etanol_screen_destroy(void)
{
    if (ui_etanol) lv_obj_delete(ui_etanol);
    ui_etanol = NULL;
    value_lbl = unit_lbl = bar = class_lbl = NULL;
}
