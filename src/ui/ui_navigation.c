#include "ui.h"
#include "ui_navigation.h"

#include <stdio.h>
#include <esp_heap_caps.h>

#define SWIPE_MIN_PX  45

/* Telas sob demanda: so a tela visivel existe na RAM. O swipe cria o destino,
 * anima, e destroi a origem quando ela sai da tela (SCREEN_UNLOADED). Nada de
 * estado mora nas telas -- conexao BT, gauges e max/min da sessao ficam em
 * bt_gateway / vehicle_data / ui_mock_data -- entao recriar e so redesenhar.
 *
 * nav_screens[] continua sendo o array simples de ponteiros: o preview le a
 * ordem das telas dele com regex. */
static lv_obj_t **nav_screens[] = { &ui_nanomode, &ui_trackmode, &ui_blelist, &ui_wifi };
static void (*const nav_init[])(void) = {
    ui_nanomode_screen_init, ui_trackmode_screen_init, ui_blelist_screen_init, ui_wifi_screen_init,
};
static void (*const nav_destroy[])(void) = {
    ui_nanomode_screen_destroy, ui_trackmode_screen_destroy, ui_blelist_screen_destroy, ui_wifi_screen_destroy,
};
static const int nav_screen_count = sizeof(nav_screens) / sizeof(nav_screens[0]);
static int current_idx = -1;            /* -1 enquanto o splash esta na tela */

/* Transicao em curso: a tela antiga ainda anima para fora e sera destruida
 * quando descarregar. Swipes nesse intervalo (300 ms) sao ignorados para a
 * destruicao nunca alcancar uma tela recem-recriada. */
static bool nav_busy = false;
static void (*pending_destroy)(void) = NULL;
static const char *pending_name = "";

static int16_t swipe_start_x = 0;
static bool swipe_tracking = false;
static bool swipe_handled = false;

bool ui_navigation_swipe_consumed(void)
{
    if (!swipe_handled) return false;
    swipe_handled = false;
    return true;
}

static void nav_pointer_event_cb(lv_event_t *e);

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

static void log_heap(const char *what)
{
    printf("[HEAP] %-14s livre %6u B, maior bloco %6u B\n", what,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

/* Roda no proximo lv_timer_handler(), fora do evento da tela que esta saindo:
 * deletar um objeto de dentro do proprio callback e pedir problema. */
static void destroy_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (pending_destroy) pending_destroy();
    pending_destroy = NULL;
    nav_busy = false;
    log_heap(pending_name);
}

static void screen_unloaded_cb(lv_event_t *e)
{
    (void)e;
    lv_timer_t *t = lv_timer_create(destroy_timer_cb, 0, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void schedule_destroy(lv_obj_t *screen, void (*destroy)(void), const char *name)
{
    pending_destroy = destroy;
    pending_name = name;
    nav_busy = true;
    lv_obj_add_event_cb(screen, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
}

static const char *const nav_names[] = { "-> nanomode", "-> trackmode", "-> blelist", "-> wifi" };

static void nav_goto(int idx, lv_screen_load_anim_t anim)
{
    if (idx < 0 || idx >= nav_screen_count || nav_busy || idx == current_idx) return;

    if (*nav_screens[idx] == NULL) {
        nav_init[idx]();
        bind_nav_screen(*nav_screens[idx]);
        ui_mock_data_refresh();           /* labels novos ja nascem com o valor atual */
    }

    if (current_idx >= 0) {
        schedule_destroy(*nav_screens[current_idx], nav_destroy[current_idx], nav_names[idx]);
    } else if (ui_splash) {
        schedule_destroy(ui_splash, ui_splash_screen_destroy, nav_names[idx]);
    }

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
}
