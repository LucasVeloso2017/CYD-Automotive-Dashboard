#include "../ui.h"

lv_obj_t *uic_baterynano, *uic_temperaturenano, *uic_lambdanano, *uic_nanomode;
lv_obj_t *ui_nanomode = NULL, *ui_Image1 = NULL;
lv_obj_t *ui_lambdanano = NULL, *ui_temperaturenano = NULL, *ui_baterynano = NULL;

void ui_nanomode_screen_init(void)
{
    ui_nanomode = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_nanomode, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_nanomode, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_nanomode, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Image1 = lv_image_create(ui_nanomode);
    lv_image_set_src(ui_Image1, &ui_img_2056485965);
    lv_obj_set_width(ui_Image1, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Image1, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_Image1, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Image1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_lambdanano = lv_label_create(ui_nanomode);
    lv_obj_set_width(ui_lambdanano, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_lambdanano, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_lambdanano, 4);
    lv_obj_set_y(ui_lambdanano, 0);
    lv_obj_set_align(ui_lambdanano, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lambdanano, "00");
    lv_obj_set_style_text_color(ui_lambdanano, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lambdanano, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lambdanano, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_temperaturenano = lv_label_create(ui_nanomode);
    lv_obj_set_width(ui_temperaturenano, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_temperaturenano, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_temperaturenano, -114);
    lv_obj_set_y(ui_temperaturenano, 93);
    lv_obj_set_align(ui_temperaturenano, LV_ALIGN_CENTER);
    lv_label_set_text(ui_temperaturenano, "00");
    lv_obj_set_style_text_color(ui_temperaturenano, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_temperaturenano, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_temperaturenano, LV_TEXT_ALIGN_AUTO, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_temperaturenano, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_baterynano = lv_label_create(ui_nanomode);
    lv_obj_set_width(ui_baterynano, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_baterynano, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_baterynano, 116);
    lv_obj_set_y(ui_baterynano, 94);
    lv_obj_set_align(ui_baterynano, LV_ALIGN_CENTER);
    lv_label_set_text(ui_baterynano, "00");
    lv_obj_set_style_text_color(ui_baterynano, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_baterynano, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_baterynano, LV_TEXT_ALIGN_AUTO, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_baterynano, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    uic_nanomode = ui_nanomode;
    uic_lambdanano = ui_lambdanano;
    uic_temperaturenano = ui_temperaturenano;
    uic_baterynano = ui_baterynano;
}

void ui_nanomode_screen_destroy(void)
{
    if (ui_nanomode) lv_obj_delete(ui_nanomode);
    uic_nanomode = NULL; ui_nanomode = NULL; ui_Image1 = NULL;
    uic_lambdanano = NULL; ui_lambdanano = NULL;
    uic_temperaturenano = NULL; ui_temperaturenano = NULL;
    uic_baterynano = NULL; ui_baterynano = NULL;
}
