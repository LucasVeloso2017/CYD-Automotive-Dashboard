#ifndef UI_NAVIGATION_H
#define UI_NAVIGATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

#define SPLASH_DURATION_MS       2500
#define SCREEN_TRANSITION_MS     300

void ui_navigation_init(void);
void ui_navigation_apply_bubble(lv_obj_t *obj);
void ui_navigation_bind_pointer(lv_obj_t *obj);
void ui_navigation_refresh_blelist(void);
bool ui_navigation_swipe_consumed(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
