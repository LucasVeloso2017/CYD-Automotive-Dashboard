#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui____initial_actions0;

///////////////////// ANIMATIONS ////////////////////
#if UI_ENABLE_SPLASH
lv_anim_t * splash_Animation(lv_obj_t *TargetObject, int delay)
{
    lv_anim_t *out_anim;
    ui_anim_user_data_t *PropertyAnimation_0_user_data = lv_malloc(sizeof(ui_anim_user_data_t));
    PropertyAnimation_0_user_data->target = TargetObject;
    PropertyAnimation_0_user_data->val = -1;
    lv_anim_t PropertyAnimation_0;
    lv_anim_init(&PropertyAnimation_0);
    lv_anim_set_time(&PropertyAnimation_0, 2000);
    lv_anim_set_user_data(&PropertyAnimation_0, PropertyAnimation_0_user_data);
    lv_anim_set_custom_exec_cb(&PropertyAnimation_0, _ui_anim_callback_set_opacity);
    lv_anim_set_values(&PropertyAnimation_0, 0, 255);
    lv_anim_set_path_cb(&PropertyAnimation_0, lv_anim_path_ease_in_out);
    lv_anim_set_delay(&PropertyAnimation_0, delay + 100);
    lv_anim_set_deleted_cb(&PropertyAnimation_0, _ui_anim_callback_free_user_data);
    lv_anim_set_playback_time(&PropertyAnimation_0, 0);
    lv_anim_set_playback_delay(&PropertyAnimation_0, 0);
    lv_anim_set_repeat_count(&PropertyAnimation_0, 0);
    lv_anim_set_repeat_delay(&PropertyAnimation_0, 0);
    lv_anim_set_early_apply(&PropertyAnimation_0, false);
    lv_anim_set_get_value_cb(&PropertyAnimation_0, &_ui_anim_callback_get_opacity);
    out_anim = lv_anim_start(&PropertyAnimation_0);

    return out_anim;
}
#endif /* UI_ENABLE_SPLASH */

///////////////////// SCREENS ////////////////////

void ui_init(void)
{
#if UI_ENABLE_SPLASH
    ui_splash_screen_init();
#endif
    ui_nanomode_screen_init();
    ui_trackmode_screen_init();
    ui_etanol_screen_init();
    ui_blelist_screen_init();
    ui_dtc_screen_init();
#if UI_ENABLE_WEB
    ui_qr_screen_init();
#endif
    ui_config_screen_init();
    ui____initial_actions0 = lv_obj_create(NULL);
#if UI_ENABLE_SPLASH
    lv_screen_load(ui_splash);
#else
    lv_screen_load(ui_nanomode);      /* sem splash: o boot abre direto no nanomode */
#endif
    ui_navigation_init();
    ui_mock_data_init();
    ui_statusbar_init();
    ui_autoconnect_init();          /* por ultimo: a camada de loading fica por cima da barra */
}

void ui_destroy(void)
{
#if UI_ENABLE_SPLASH
    ui_splash_screen_destroy();
#endif
    ui_nanomode_screen_destroy();
    ui_trackmode_screen_destroy();
    ui_etanol_screen_destroy();
    ui_blelist_screen_destroy();
    ui_dtc_screen_destroy();
#if UI_ENABLE_WEB
    ui_qr_screen_destroy();
#endif
    ui_config_screen_destroy();
}
