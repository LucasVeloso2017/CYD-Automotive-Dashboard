#ifndef _SQUARELINE_PROJECT_UI_H
#define _SQUARELINE_PROJECT_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined __has_include
  #if __has_include("lvgl.h")
    #include "lvgl.h"
  #elif __has_include("lvgl/lvgl.h")
    #include "lvgl/lvgl.h"
  #else
    #include "lvgl.h"
  #endif
#else
  #include "lvgl.h"
#endif

/* Splash screen. 0 = fora do build: o codigo continua no fonte, mas o
 * pre-processador tira ui_splash.c, o asset e a animacao do binario, e o boot
 * vai direto ao nanomode. Para reativar, basta 1. (O preview le esta flag.) */
#ifndef UI_ENABLE_SPLASH
#define UI_ENABLE_SPLASH 0
#endif

/* Pagina web pelo celular (tela do QR + link serial com o Wemos D1 mini, em
 * wemos-portal/). 0 = fora do build: a tela do QR sai do swipe e do binario, e
 * a serial para o Wemos (GPIO 27/22) nem e inicializada. O codigo fica no
 * fonte; para reativar, basta 1 (ou -DUI_ENABLE_WEB=1). O preview le esta flag. */
#ifndef UI_ENABLE_WEB
#define UI_ENABLE_WEB 0
#endif

#include "ui_helpers.h"
#include "ui_events.h"
#include "ui_navigation.h"
#include "ui_mock_data.h"
#include "ui_statusbar.h"
#include "ui_autoconnect.h"

///////////////////// SCREENS ////////////////////
#if UI_ENABLE_SPLASH
#include "views/ui_splash.h"
#endif
#include "views/ui_nanomode.h"
#include "views/ui_trackmode.h"
#include "views/ui_etanol.h"
#include "views/ui_blelist.h"
#include "views/ui_dtc.h"
#if UI_ENABLE_WEB
#include "views/ui_qr.h"
#endif
#include "views/ui_config.h"

///////////////////// VARIABLES ////////////////////
#if UI_ENABLE_SPLASH
extern lv_anim_t * splash_Animation(lv_obj_t *TargetObject, int delay);
#endif

extern lv_obj_t *ui____initial_actions0;

#if UI_ENABLE_SPLASH
LV_IMAGE_DECLARE(ui_img_rent4fun);
#endif
LV_IMAGE_DECLARE(ui_img_2056485965);
LV_IMAGE_DECLARE(ui_img_1105076504);
LV_IMAGE_DECLARE(ui_img_1706062683);

void ui_init(void);
void ui_destroy(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
