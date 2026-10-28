#include "../ui.h"
#include "../ui_navigation.h"
#include "../../display/display.h"

/* Configuracoes: brilho do backlight e tela girada 180 graus.
 *
 * As duas valem na hora e ficam na NVS (src/display/display.cpp). Textos em
 * ASCII: as fontes Montserrat do LVGL nao tem acentos. O preview le os
 * "#define CFG_NOME valor" deste arquivo; mantenha o formato. */
#define CFG_TITLE_Y           18
#define CFG_CARD_X            16
#define CFG_CARD_W            288
#define CFG_CARD_PAD          12
#define CFG_CARD_RADIUS       8
#define CFG_BRIGHT_Y          50
#define CFG_BRIGHT_H          78
#define CFG_ROTATE_Y          140
#define CFG_ROTATE_H          54
#define CFG_SLIDER_H          8
#define CFG_SWITCH_W          50
#define CFG_SWITCH_H          26
#define CFG_FOOTER_Y          214

#define CFG_COLOR_TEXT        0xFFFFFF
#define CFG_COLOR_MUTED       0x8B96A5
#define CFG_COLOR_ACCENT      0xFF9B00
#define CFG_COLOR_CARD        0x1C1C1C
#define CFG_COLOR_TRACK       0x333333

lv_obj_t *ui_config = NULL;

static lv_obj_t *bright_val, *slider, *sw;

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    return l;
}

static lv_obj_t *card(int16_t y, int16_t h)
{
    lv_obj_t *c = lv_obj_create(ui_config);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, CFG_CARD_X, y);
    lv_obj_set_size(c, CFG_CARD_W, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(CFG_COLOR_CARD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(c, CFG_CARD_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(c, CFG_CARD_PAD, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static void show_brightness(uint8_t pct)
{
    lv_label_set_text_fmt(bright_val, "%u%%", (unsigned)pct);
}

static void slider_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        const uint8_t pct = (uint8_t)lv_slider_get_value(slider);
        display_set_brightness(pct);          /* na hora, a cada passo do arraste */
        show_brightness(display_brightness());
    } else if (code == LV_EVENT_RELEASED) {
        display_save_brightness();            /* NVS so no fim: o arraste gera dezenas de passos */
    }
}

static void switch_cb(lv_event_t *e)
{
    (void)e;
    const bool want = lv_obj_has_state(sw, LV_STATE_CHECKED);
    /* Um swipe que comecou em cima do switch ja trocou de tela; nao vale como toque. */
    if (ui_navigation_swipe_consumed()) {
        if (want) lv_obj_clear_state(sw, LV_STATE_CHECKED);
        else      lv_obj_add_state(sw, LV_STATE_CHECKED);
        return;
    }
    display_set_flipped(want);
    /* O painel guarda o quadro antigo na orientacao antiga: redesenha tudo,
     * inclusive a barra de status na camada de cima. */
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
}

void ui_config_screen_init(void)
{
    ui_config = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_config, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_config, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_config, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = text(ui_config, "CONFIGURACOES", &lv_font_montserrat_16, CFG_COLOR_MUTED);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, CFG_TITLE_Y);

    /* ---- brilho ---- */
    lv_obj_t *c = card(CFG_BRIGHT_Y, CFG_BRIGHT_H);
    lv_obj_t *l = text(c, "Brilho", &lv_font_montserrat_16, CFG_COLOR_TEXT);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
    bright_val = text(c, "", &lv_font_montserrat_16, CFG_COLOR_ACCENT);
    lv_obj_align(bright_val, LV_ALIGN_TOP_RIGHT, 0, 0);

    slider = lv_slider_create(c);
    lv_obj_set_size(slider, CFG_CARD_W - 2 * CFG_CARD_PAD - 16, CFG_SLIDER_H);   /* 16: folga para o knob nas pontas */
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_slider_set_range(slider, DISPLAY_BRIGHTNESS_MIN, DISPLAY_BRIGHTNESS_MAX);
    lv_slider_set_value(slider, display_brightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(CFG_COLOR_TRACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(CFG_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(CFG_COLOR_TEXT), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB | LV_STATE_DEFAULT);
    /* area de toque maior que a barra fina: no touch resistivo o dedo erra 8 px */
    lv_obj_set_ext_click_area(slider, 14);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_RELEASED, NULL);
    show_brightness(display_brightness());

    /* ---- rotacao ---- */
    c = card(CFG_ROTATE_Y, CFG_ROTATE_H);
    l = text(c, "Girar tela 180 graus", &lv_font_montserrat_16, CFG_COLOR_TEXT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

    sw = lv_switch_create(c);
    lv_obj_set_size(sw, CFG_SWITCH_W, CFG_SWITCH_H);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(CFG_COLOR_TRACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sw, lv_color_hex(CFG_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (display_flipped()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    l = text(ui_config, "Salvo automaticamente", &lv_font_montserrat_12, CFG_COLOR_MUTED);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, CFG_FOOTER_Y);
}

void ui_config_screen_destroy(void)
{
    if (ui_config) lv_obj_delete(ui_config);
    ui_config = NULL;
    bright_val = slider = sw = NULL;
}
