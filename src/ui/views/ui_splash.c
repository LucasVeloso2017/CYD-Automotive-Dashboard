#include "../ui.h"

lv_obj_t *uic_splash;
lv_obj_t *ui_splash = NULL;
lv_obj_t *ui_Image2 = NULL;

void ui_splash_screen_init(void)
{
    ui_splash = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_splash, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_splash, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Image2 = lv_image_create(ui_splash);
    lv_image_set_src(ui_Image2, &ui_img_audi_s3);
    lv_obj_set_width(ui_Image2, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Image2, LV_SIZE_CONTENT);
    // o recorte cai exatamente no centro da arte de 320x240, entao
    // LV_ALIGN_CENTER sozinho ja reproduz a composicao original
    lv_obj_set_align(ui_Image2, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Image2, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_Image2, LV_OBJ_FLAG_SCROLLABLE);

    uic_splash = ui_splash;
}

void ui_splash_screen_destroy(void)
{
    if (ui_splash) lv_obj_delete(ui_splash);
    uic_splash = NULL;
    ui_splash = NULL;
    ui_Image2 = NULL;
}
