#include "../ui.h"

lv_obj_t *uic_maxbaterytrack1, *uic_baterytrack, *uic_maxlambdatrack, *uic_lambdatrack;
lv_obj_t *uic_maxoiltrack, *uic_oiltrack, *uic_maxiattrack1, *uic_iattrack;
lv_obj_t *uic_maxcombttrack, *uic_combpressuretrack, *uic_maxbostttrack1;
lv_obj_t *uic_boosttrack, *uic_trackmode;

lv_obj_t *ui_trackmode = NULL, *ui_Image3 = NULL;
lv_obj_t *ui_boosttrack = NULL, *ui_maxcontainer1 = NULL, *ui_maxbostttrack1 = NULL, *ui_label1 = NULL;
lv_obj_t *ui_combpressuretrack = NULL, *ui_maxcontainer = NULL, *ui_maxcombttrack = NULL, *ui_label = NULL;
lv_obj_t *ui_iattrack = NULL, *ui_maxcontainer2 = NULL, *ui_maxiattrack1 = NULL, *ui_label2 = NULL;
lv_obj_t *ui_oiltrack = NULL, *ui_maxcontainer3 = NULL, *ui_maxoiltrack = NULL, *ui_label3 = NULL;
lv_obj_t *ui_lambdatrack = NULL, *ui_maxcontainer4 = NULL, *ui_maxlambdatrack = NULL, *ui_label4 = NULL;
lv_obj_t *ui_baterytrack = NULL, *ui_maxcontainer5 = NULL, *ui_maxbaterytrack1 = NULL, *ui_label5 = NULL;

static void _max_container(lv_obj_t *parent, lv_obj_t **cont, lv_obj_t **val_lbl, lv_obj_t **max_lbl,
                            int16_t x, int16_t y, int16_t w, int16_t lbl3_x,
                            lv_flex_align_t cross)
{
    *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(*cont);
    lv_obj_set_width(*cont, w);
    lv_obj_set_height(*cont, 16);
    lv_obj_set_x(*cont, x);
    lv_obj_set_y(*cont, y);
    lv_obj_set_align(*cont, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(*cont, LV_FLEX_FLOW_ROW_REVERSE);
    lv_obj_set_flex_align(*cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, cross);
    lv_obj_clear_flag(*cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(*cont, lv_color_hex(0xFFFFFF), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(*cont, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(*cont, 10, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(*cont, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    *val_lbl = lv_label_create(*cont);
    lv_obj_set_width(*val_lbl, LV_SIZE_CONTENT);
    lv_obj_set_height(*val_lbl, LV_SIZE_CONTENT);
    lv_obj_set_x(*val_lbl, 14); lv_obj_set_y(*val_lbl, 0);
    lv_obj_set_align(*val_lbl, LV_ALIGN_CENTER);
    lv_label_set_text(*val_lbl, "00");
    lv_obj_set_style_text_color(*val_lbl, lv_color_hex(0xFF9B00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(*val_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(*val_lbl, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    *max_lbl = lv_label_create(*cont);
    lv_obj_set_width(*max_lbl, LV_SIZE_CONTENT);
    lv_obj_set_height(*max_lbl, LV_SIZE_CONTENT);
    lv_obj_set_x(*max_lbl, lbl3_x); lv_obj_set_y(*max_lbl, -1);
    lv_obj_set_align(*max_lbl, LV_ALIGN_CENTER);
    lv_label_set_text(*max_lbl, "max");
    lv_obj_set_style_text_color(*max_lbl, lv_color_hex(0xFF9B00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(*max_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(*max_lbl, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t * _gauge(lv_obj_t *parent, int16_t x, int16_t y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, LV_SIZE_CONTENT);
    lv_obj_set_height(lbl, LV_SIZE_CONTENT);
    lv_obj_set_x(lbl, x); lv_obj_set_y(lbl, y);
    lv_obj_set_align(lbl, LV_ALIGN_CENTER);
    lv_label_set_text(lbl, "00");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    return lbl;
}

void ui_trackmode_screen_init(void)
{
    ui_trackmode = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_trackmode, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_trackmode, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_trackmode, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Image3 = lv_image_create(ui_trackmode);
    lv_image_set_src(ui_Image3, &ui_img_1105076504);
    lv_obj_set_width(ui_Image3, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Image3, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_Image3, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Image3, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_boosttrack = _gauge(ui_trackmode, -82, -94);
    _max_container(ui_trackmode, &ui_maxcontainer1, &ui_maxbostttrack1, &ui_label1, -78, -67, 96, -15, LV_FLEX_ALIGN_START);

    ui_combpressuretrack = _gauge(ui_trackmode, 80, -95);
    _max_container(ui_trackmode, &ui_maxcontainer, &ui_maxcombttrack, &ui_label, 82, -67, 96, -15, LV_FLEX_ALIGN_START);

    ui_iattrack = _gauge(ui_trackmode, -83, -13);
    _max_container(ui_trackmode, &ui_maxcontainer2, &ui_maxiattrack1, &ui_label2, -81, 18, 93, -15, LV_FLEX_ALIGN_START);

    ui_oiltrack = _gauge(ui_trackmode, 82, -13);
    _max_container(ui_trackmode, &ui_maxcontainer3, &ui_maxoiltrack, &ui_label3, 81, 19, 93, -6, LV_FLEX_ALIGN_SPACE_EVENLY);

    ui_lambdatrack = _gauge(ui_trackmode, -83, 68);
    _max_container(ui_trackmode, &ui_maxcontainer4, &ui_maxlambdatrack, &ui_label4, -82, 91, 94, -15, LV_FLEX_ALIGN_START);

    ui_baterytrack = _gauge(ui_trackmode, 79, 69);
    _max_container(ui_trackmode, &ui_maxcontainer5, &ui_maxbaterytrack1, &ui_label5, 80, 92, 98, -15, LV_FLEX_ALIGN_START);

    uic_trackmode = ui_trackmode;
    uic_boosttrack = ui_boosttrack; uic_maxbostttrack1 = ui_maxbostttrack1;
    uic_combpressuretrack = ui_combpressuretrack; uic_maxcombttrack = ui_maxcombttrack;
    uic_iattrack = ui_iattrack; uic_maxiattrack1 = ui_maxiattrack1;
    uic_oiltrack = ui_oiltrack; uic_maxoiltrack = ui_maxoiltrack;
    uic_lambdatrack = ui_lambdatrack; uic_maxlambdatrack = ui_maxlambdatrack;
    uic_baterytrack = ui_baterytrack; uic_maxbaterytrack1 = ui_maxbaterytrack1;
}

void ui_trackmode_screen_destroy(void)
{
    if (ui_trackmode) lv_obj_delete(ui_trackmode);
    uic_trackmode = NULL; ui_trackmode = NULL; ui_Image3 = NULL;
    uic_boosttrack = NULL; ui_boosttrack = NULL;
    ui_maxcontainer1 = NULL; uic_maxbostttrack1 = NULL; ui_maxbostttrack1 = NULL; ui_label1 = NULL;
    uic_combpressuretrack = NULL; ui_combpressuretrack = NULL;
    ui_maxcontainer = NULL; uic_maxcombttrack = NULL; ui_maxcombttrack = NULL; ui_label = NULL;
    uic_iattrack = NULL; ui_iattrack = NULL;
    ui_maxcontainer2 = NULL; uic_maxiattrack1 = NULL; ui_maxiattrack1 = NULL; ui_label2 = NULL;
    uic_oiltrack = NULL; ui_oiltrack = NULL;
    ui_maxcontainer3 = NULL; uic_maxoiltrack = NULL; ui_maxoiltrack = NULL; ui_label3 = NULL;
    uic_lambdatrack = NULL; ui_lambdatrack = NULL;
    ui_maxcontainer4 = NULL; uic_maxlambdatrack = NULL; ui_maxlambdatrack = NULL; ui_label4 = NULL;
    uic_baterytrack = NULL; ui_baterytrack = NULL;
    ui_maxcontainer5 = NULL; uic_maxbaterytrack1 = NULL; ui_maxbaterytrack1 = NULL; ui_label5 = NULL;
}
