#include "../ui.h"
#include "../ui_navigation.h"
#include "../../net/track_server.h"
#include <string.h>

/* QR de 200 px com quiet zone do proprio widget (4 modulos brancos de cada
 * lado): o payload Wi-Fi cabe na versao 2 ou 3, o que da 5-6 px por modulo
 * (~1 mm no CYD), escaneavel a um braco de distancia.
 *
 * Enquanto esta tela existe o Bluetooth fica suspenso e o Wi-Fi no ar: os
 * dois nao cabem juntos na RAM do WROOM. A pagina mostra o resumo da ultima
 * sessao (max/min ficam em ui_mock_data, fora das telas e do BT). */
#define WIFI_QR_SIZE       200
#define WIFI_QR_X          10
#define WIFI_TEXT_X        220
#define WIFI_TEXT_W        92
#define WIFI_STATUS_MS     1000

lv_obj_t *ui_wifi = NULL;

static lv_obj_t *qr;
static lv_obj_t *status_lbl;
static lv_timer_t *status_timer;

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    return l;
}

static void status_refresh(void)
{
    if (!status_lbl) return;
    if (track_server_failed()) {
        lv_label_set_text(status_lbl, "AP falhou\n(ver serial)");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF4D4D), LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }
    if (!track_server_running()) {
        lv_label_set_text(status_lbl, "pausando BT,\nligando AP...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x8B96A5), LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }
    uint8_t n = track_server_clients();
    lv_label_set_text_fmt(status_lbl, LV_SYMBOL_WIFI " %u online", n);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(n ? 0x2FD27A : 0xFFC233), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    status_refresh();
}

static void screen_loaded_cb(lv_event_t *e)
{
    (void)e;
    track_server_request_start();   /* o loop() sobe o AP fora da pilha do LVGL */
    status_refresh();
}

void ui_wifi_screen_init(void)
{
    ui_wifi = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_wifi, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_wifi, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_wifi, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

    /* quiet_zone precisa vir antes do update: e ele que escolhe versao e escala. */
    qr = lv_qrcode_create(ui_wifi);
    lv_qrcode_set_size(qr, WIFI_QR_SIZE);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_quiet_zone(qr, true);
    if (lv_qrcode_update(qr, TRACK_AP_QR, strlen(TRACK_AP_QR)) != LV_RESULT_OK) {
        LV_LOG_ERROR("QR do Wi-Fi nao coube em %d px", WIFI_QR_SIZE);
    }
    lv_obj_align(qr, LV_ALIGN_LEFT_MID, WIFI_QR_X, 0);
    lv_obj_clear_flag(qr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* Coluna de texto a direita: instrucoes e credenciais para quem preferir digitar. */
    lv_obj_t *col = lv_obj_create(ui_wifi);
    lv_obj_set_pos(col, WIFI_TEXT_X, 22);
    lv_obj_set_size(col, WIFI_TEXT_W, 240 - 44);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(col, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    text(col, "TRACK",   &lv_font_montserrat_16, 0xFF7A00);
    text(col, "METRICS", &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_t *hint = text(col, "Resumo da sessao no celular. BT pausado nesta tela.", &lv_font_montserrat_10, 0x8B96A5);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_bottom(hint, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

    text(col, "Rede",  &lv_font_montserrat_10, 0x8B96A5);
    text(col, TRACK_AP_SSID, &lv_font_montserrat_16, 0xFFFFFF);
    text(col, "Senha", &lv_font_montserrat_10, 0x8B96A5);
    text(col, TRACK_AP_PASS, &lv_font_montserrat_16, 0xFFFFFF);
    text(col, "Site",  &lv_font_montserrat_10, 0x8B96A5);
    text(col, "192.168.4.1", &lv_font_montserrat_14, 0xFFFFFF);

    status_lbl = text(col, "", &lv_font_montserrat_12, 0x8B96A5);
    lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(status_lbl, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    status_refresh();

    status_timer = lv_timer_create(status_timer_cb, WIFI_STATUS_MS, NULL);
}

void ui_wifi_screen_destroy(void)
{
    track_server_request_stop();    /* o loop() derruba o AP e religa o Bluetooth */
    if (status_timer) lv_timer_delete(status_timer);
    status_timer = NULL;
    if (ui_wifi) lv_obj_delete(ui_wifi);
    ui_wifi = NULL;
    qr = NULL;
    status_lbl = NULL;
}
