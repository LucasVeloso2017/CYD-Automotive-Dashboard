#include "ui.h"
#include "ui_navigation.h"

#define SWIPE_MIN_PX  45

static lv_obj_t **nav_screens[] = { &ui_nanomode, &ui_trackmode, &ui_blelist };
static const int nav_screen_count = sizeof(nav_screens) / sizeof(nav_screens[0]);
static int current_idx = 0;

static int16_t swipe_start_x = 0;
static bool swipe_tracking = false;
static bool swipe_handled = false;

bool ui_navigation_swipe_consumed(void)
{
    if (!swipe_handled) return false;
    swipe_handled = false;
    return true;
}

static void nav_goto(int idx, lv_screen_load_anim_t anim)
{
    if (idx < 0 || idx >= nav_screen_count) return;
    current_idx = idx;
    lv_screen_load_anim(*nav_screens[idx], anim, SCREEN_TRANSITION_MS, 0, false);
}

static void splash_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    nav_goto(0, LV_SCR_LOAD_ANIM_FADE_ON);
}

static void nav_pointer_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    if (code == LV_EVENT_PRESSED) {
        swipe_start_x = pt.x;
        swipe_tracking = true;
        swipe_handled = false;
        return;
    }

    if (code != LV_EVENT_RELEASED || !swipe_tracking) return;
    swipe_tracking = false;

    int16_t dx = pt.x - swipe_start_x;
    if (dx <= -SWIPE_MIN_PX && current_idx < nav_screen_count - 1) {
        swipe_handled = true;
        nav_goto(current_idx + 1, LV_SCR_LOAD_ANIM_MOVE_LEFT);
    } else if (dx >= SWIPE_MIN_PX && current_idx > 0) {
        swipe_handled = true;
        nav_goto(current_idx - 1, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    }
}

void ui_navigation_apply_bubble(lv_obj_t *obj)
{
    if (!obj) return;

    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);

    uint32_t cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < cnt; i++) {
        ui_navigation_apply_bubble(lv_obj_get_child(obj, i));
    }
}

static void bind_nav_screen(lv_obj_t *screen)
{
    if (!screen) return;

    lv_obj_add_event_cb(screen, nav_pointer_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen, nav_pointer_event_cb, LV_EVENT_RELEASED, NULL);
    ui_navigation_apply_bubble(screen);
}

void ui_navigation_bind_pointer(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_add_event_cb(obj, nav_pointer_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, nav_pointer_event_cb, LV_EVENT_RELEASED, NULL);
}

void ui_navigation_refresh_blelist(void)
{
    if (ui_blelist) ui_navigation_apply_bubble(ui_blelist);
}

void ui_navigation_init(void)
{
    splash_Animation(ui_Image2, 0);

    lv_timer_t *t = lv_timer_create(splash_timer_cb, SPLASH_DURATION_MS, NULL);
    lv_timer_set_repeat_count(t, 1);

    for (int i = 0; i < nav_screen_count; i++) {
        bind_nav_screen(*nav_screens[i]);
    }
}
