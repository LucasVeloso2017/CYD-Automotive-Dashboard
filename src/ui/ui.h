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

#include "ui_helpers.h"
#include "ui_events.h"
#include "ui_navigation.h"
#include "ui_mock_data.h"

///////////////////// SCREENS ////////////////////
#include "views/ui_splash.h"
#include "views/ui_nanomode.h"
#include "views/ui_trackmode.h"
#include "views/ui_blelist.h"

///////////////////// VARIABLES ////////////////////
extern lv_anim_t * splash_Animation(lv_obj_t *TargetObject, int delay);

extern lv_obj_t *ui____initial_actions0;

LV_IMAGE_DECLARE(ui_img_gli);
LV_IMAGE_DECLARE(ui_img_2056485965);
LV_IMAGE_DECLARE(ui_img_1105076504);
LV_IMAGE_DECLARE(ui_img_1706062683);

void ui_init(void);
void ui_destroy(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
