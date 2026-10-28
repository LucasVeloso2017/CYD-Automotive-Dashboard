#include "ui.h"
#include "ui_navigation.h"

#define SWIPE_MIN_PX  45

/* Todas as telas nascem no boot (ui_init) e vivem o tempo todo.
 *
 * Por um tempo elas foram criadas sob demanda, para abrir memoria ao WiFi que
 * rodava no proprio CYD. O WiFi saiu do CYD (foi para o Wemos, e hoje esta
 * desligado: UI_ENABLE_WEB), e a criacao sob demanda cobrava um engasgo no
 * comeco de cada slide — o trackmode tem dezenas de objetos para montar no
 * instante do gesto. Com a memoria folgada de novo, criar tudo no boot deixa o
 * slide so com o trabalho de desenhar.
 *
 * O preview le a ordem das telas desta lista por regex (um &ui_* por linha) e
 * respeita os #if de flag de ui.h. */
static lv_obj_t **const nav_screens[] = {
    &ui_nanomode,
    &ui_trackmode,
    &ui_etanol,
    &ui_blelist,
    &ui_dtc,
#if UI_ENABLE_WEB
    &ui_qr,
#endif
    &ui_config,
};
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

/* lv_screen_load_anim sempre anima em linha reta (lv_anim_init deixa o path
 * linear) e nao expoe o easing. As duas animacoes -- a da tela que entra e a
 * da que sai -- ja estao na lista quando ela retorna, entao trocamos o path
 * aqui. NULL = primeira animacao do objeto; telas nao tem outras.
 *
 * ease-in-out: acelera devagar e freia devagar. O ease-out de antes arrancava
 * na velocidade maxima no primeiro quadro, justamente quando o display ainda
 * esta mandando o primeiro quadro da tela nova — o salto inicial era o que mais
 * aparecia como tranco. */
static void ease_screen_anim(lv_obj_t *scr)
{
    lv_anim_t *a = lv_anim_get(scr, NULL);
    if (a) lv_anim_set_path_cb(a, lv_anim_path_ease_in_out);
}

static void nav_goto(int idx, lv_screen_load_anim_t anim)
{
    if (idx < 0 || idx >= nav_screen_count) return;
    current_idx = idx;
    lv_obj_t *old_scr = lv_screen_active();
    lv_obj_t *new_scr = *nav_screens[idx];
    lv_screen_load_anim(new_scr, anim, SCREEN_TRANSITION_MS, 0, false);
    ease_screen_anim(new_scr);
    if (old_scr && old_scr != new_scr) ease_screen_anim(old_scr);
}

#if UI_ENABLE_SPLASH
static void splash_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    nav_goto(0, LV_SCR_LOAD_ANIM_FADE_ON);
}
#endif

static void nav_pointer_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    if (code == LV_EVENT_PRESSED) {
        /* Arrastar um slider (brilho, na tela de configuracoes) e mexer nele,
         * nao trocar de tela: o arraste horizontal passaria do limiar do swipe. */
        swipe_tracking = !lv_obj_check_type(lv_event_get_target_obj(e), &lv_slider_class);
        swipe_start_x = pt.x;
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
#if UI_ENABLE_SPLASH
    splash_Animation(ui_Image2, 0);

    lv_timer_t *t = lv_timer_create(splash_timer_cb, SPLASH_DURATION_MS, NULL);
    lv_timer_set_repeat_count(t, 1);
#endif

    for (int i = 0; i < nav_screen_count; i++) {
        bind_nav_screen(*nav_screens[i]);
    }
}
