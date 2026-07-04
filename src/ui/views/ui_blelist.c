#include "../ui.h"
#include "../ui_navigation.h"
#include "../../gateway/bt_gateway.h"
#include <string.h>

#define BLELIST_ITEM_H      56
#define BLELIST_ITEM_GAP    6
#define BLELIST_PAD_TOP     35

lv_obj_t *ui_blelist = NULL;
lv_obj_t *ui_Image4 = NULL;

static lv_obj_t *list_scroll;
static lv_obj_t *list_container;
static lv_timer_t *refresh_timer;

static uint8_t last_count = 0xFF;
static bool last_scanning = false;
static bool last_connecting = false;
static bool last_connected = false;
static char last_connected_addr[BT_ADDR_LEN] = "";

static void apply_item_style(lv_obj_t *item, bool connected)
{
    lv_color_t bg = connected ? lv_color_hex(0x00AA44) : lv_color_hex(0x333333);
    lv_obj_set_style_bg_color(item, bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(item, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(item, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(item, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void device_click_cb(lv_event_t *e)
{
    if (ui_navigation_swipe_consumed()) return;
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (bt_state.connecting) return;
    bt_connect(idx);
    ui_blelist_refresh();
}

static void rebuild_device_list(void)
{
    lv_obj_clean(list_container);

    if (bt_state.count == 0) {
        lv_obj_t *empty = lv_label_create(list_container);
        lv_label_set_text(empty, bt_state.scanning ? "Buscando dispositivos..." : "Nenhum dispositivo encontrado");
        lv_obj_set_style_text_color(empty, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_pad_top(empty, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }

    for (uint8_t i = 0; i < bt_state.count; i++) {
        bt_scan_result_t *dev = &bt_state.results[i];
        bool is_connected = bt_state.connected &&
            strncmp(dev->addr, bt_state.connected_addr, BT_ADDR_LEN) == 0;

        lv_obj_t *item = lv_button_create(list_container);
        lv_obj_set_width(item, lv_pct(100));
        lv_obj_set_height(item, BLELIST_ITEM_H);
        lv_obj_add_flag(item, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        apply_item_style(item, is_connected);
        lv_obj_add_event_cb(item, device_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_label_set_text_fmt(lbl, LV_SYMBOL_BLUETOOTH "  %s\n%s  %ddBm",
                              dev->name, dev->addr, dev->rssi);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

void ui_blelist_refresh(void)
{
    bool state_changed = (bt_state.count != last_count) ||
                         (bt_state.scanning != last_scanning) ||
                         (bt_state.connecting != last_connecting) ||
                         (bt_state.connected != last_connected) ||
                         (strncmp(last_connected_addr, bt_state.connected_addr, BT_ADDR_LEN) != 0);

    if (state_changed) {
        rebuild_device_list();
        last_count = bt_state.count;
        last_scanning = bt_state.scanning;
        last_connecting = bt_state.connecting;
        last_connected = bt_state.connected;
        strncpy(last_connected_addr, bt_state.connected_addr, BT_ADDR_LEN - 1);
        last_connected_addr[BT_ADDR_LEN - 1] = '\0';
        ui_navigation_refresh_blelist();
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_blelist_refresh();
}

void ui_blelist_screen_init(void)
{
    ui_blelist = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_blelist, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_blelist, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_blelist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Image4 = lv_image_create(ui_blelist);
    lv_image_set_src(ui_Image4, &ui_img_1706062683);
    lv_obj_set_width(ui_Image4, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Image4, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_Image4, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Image4, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(ui_Image4);

    list_scroll = lv_obj_create(ui_blelist);
    lv_obj_set_size(list_scroll, 300, 240 - BLELIST_PAD_TOP);
    lv_obj_align(list_scroll, LV_ALIGN_TOP_MID, 0, BLELIST_PAD_TOP);
    lv_obj_set_style_bg_opa(list_scroll, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list_scroll, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(list_scroll, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(list_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(list_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(list_scroll, LV_DIR_VER);
    lv_obj_add_flag(list_scroll, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(list_scroll, LV_OBJ_FLAG_GESTURE_BUBBLE);

    list_container = lv_obj_create(list_scroll);
    lv_obj_set_width(list_container, lv_pct(100));
    lv_obj_set_height(list_container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(list_container, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(list_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(list_container, BLELIST_ITEM_GAP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(list_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(list_container, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(list_container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    ui_navigation_bind_pointer(list_scroll);
    ui_navigation_bind_pointer(ui_blelist);

    last_count = 0xFF;
    last_connected_addr[0] = '\0';
    bt_start_scan();
    ui_blelist_refresh();

    refresh_timer = lv_timer_create(refresh_timer_cb, 400, NULL);
}

void ui_blelist_screen_destroy(void)
{
    if (refresh_timer) {
        lv_timer_delete(refresh_timer);
        refresh_timer = NULL;
    }
    if (ui_blelist) lv_obj_delete(ui_blelist);
    ui_blelist = NULL;
    ui_Image4 = NULL;
    list_scroll = NULL;
    list_container = NULL;
}
