#include "../ui.h"

#if UI_ENABLE_WEB   /* ver ui.h: fora do build enquanto a flag for 0 */
#include "../ui_navigation.h"
#include "../../net/link.h"

#include <stdio.h>
#include <string.h>

/* Geometria e cores. O preview le todos os "#define QR_*" por regex, entao
 * mantenha o formato "#define NOME valor". Textos em ASCII: as fontes
 * Montserrat do LVGL nao tem acentos (a pagina web, essa sim, usa UTF-8). */
#define QR_PAD_X          8
#define QR_TITLE_Y        1
#define QR_STATUS_Y       20
#define QR_CODE_X         10
#define QR_CODE_Y         40
#define QR_CODE_SIZE      118
#define QR_INFO_X         140
#define QR_INFO_Y         42
#define QR_HINT_Y         172
#define QR_REFRESH_MS     500
#define QR_PAYLOAD_MAX    128    /* "WIFI:T:WPA;S:<32>;P:<64>;;" */

#define QR_COLOR_TEXT     0xFFFFFF
#define QR_COLOR_MUTED    0x8B96A5
#define QR_COLOR_OK       0x2FD27A
#define QR_COLOR_WARN     0xFFC233
#define QR_COLOR_ERR      0xFF4D4D

lv_obj_t *ui_qr = NULL;

static lv_obj_t *status_lbl;
static lv_obj_t *qrcode;
static lv_obj_t *ssid_lbl, *pass_lbl, *url_lbl;
static lv_obj_t *hint_lbl;
static lv_timer_t *timer;

/* O que esta na tela: so redesenha quando muda, senao o LVGL invalida a area
 * a cada meio segundo para desenhar o mesmo texto. */
static bool    drawn_once = false;
static bool    drawn_alive = false;
static uint8_t drawn_clients = 0xFF;
static char    drawn_qr[QR_PAYLOAD_MAX];

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    return l;
}

static void set_status(const char *s, uint32_t color)
{
    lv_label_set_text(status_lbl, s);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void refresh(bool force)
{
    if (!ui_qr) return;

    const bool alive = link_portal_alive();
    const uint8_t clients = link_portal_clients();
    const char *payload = link_portal_qr_payload();

    /* A rede chega pelo fio no primeiro W do Wemos; ate la nao ha o que
     * codificar. Quando chega (ou muda), o QR e refeito. */
    if (payload[0] && strcmp(payload, drawn_qr) != 0) {
        lv_qrcode_update(qrcode, payload, strlen(payload));
        snprintf(drawn_qr, sizeof(drawn_qr), "%s", payload);
        lv_label_set_text(ssid_lbl, link_portal_ssid());
        lv_label_set_text(pass_lbl, link_portal_password());
        lv_label_set_text(url_lbl, link_portal_url());
        force = true;
    }

    if (!force && drawn_once && alive == drawn_alive && clients == drawn_clients) return;
    drawn_once = true;
    drawn_alive = alive;
    drawn_clients = clients;

    char buf[72];
    if (!alive) {
        set_status("Wemos nao responde", QR_COLOR_ERR);
    } else if (clients == 0) {
        set_status("Wi-Fi no ar - aguardando o celular", QR_COLOR_WARN);
    } else {
        snprintf(buf, sizeof(buf), "Wi-Fi no ar - %u aparelho%s",
                 (unsigned)clients, clients == 1 ? "" : "s");
        set_status(buf, QR_COLOR_OK);
    }

    /* QR so com o Wemos respondendo: mostrado sem ele, o celular tentaria
     * entrar numa rede que pode nem existir. */
    if (alive && drawn_qr[0]) lv_obj_remove_flag(qrcode, LV_OBJ_FLAG_HIDDEN);
    else                      lv_obj_add_flag(qrcode, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(hint_lbl, alive
        ? "Aponte a camera do celular para o codigo. A pagina abre sozinha."
        : "Confira o cabo do Wemos (CN1: GPIO 27, 22 e GND) e a alimentacao de 5 V.");
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_screen_active() != ui_qr) return;   /* fora da tela nao ha o que desenhar */
    refresh(false);
}

static void screen_loaded_cb(lv_event_t *e)
{
    (void)e;
    refresh(true);
}

void ui_qr_screen_init(void)
{
    ui_qr = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_qr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_qr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_qr, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_qr, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

    lv_obj_t *title = text(ui_qr, "Acesso pelo celular", &lv_font_montserrat_16, QR_COLOR_TEXT);
    lv_obj_set_pos(title, QR_PAD_X, QR_TITLE_Y);

    status_lbl = text(ui_qr, "", &lv_font_montserrat_12, QR_COLOR_MUTED);
    lv_obj_set_pos(status_lbl, QR_PAD_X, QR_STATUS_Y);
    lv_obj_set_width(status_lbl, 320 - 2 * QR_PAD_X);
    lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_DOT);

    /* Fundo claro e zona de silencio: sem isso o leitor do celular nao acha o
     * codigo em cima do preto da tela. */
    qrcode = lv_qrcode_create(ui_qr);
    lv_qrcode_set_size(qrcode, QR_CODE_SIZE);
    lv_qrcode_set_dark_color(qrcode, lv_color_black());
    lv_qrcode_set_light_color(qrcode, lv_color_white());
    lv_qrcode_set_quiet_zone(qrcode, true);
    lv_obj_set_pos(qrcode, QR_CODE_X, QR_CODE_Y);
    lv_obj_remove_flag(qrcode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(qrcode, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *k1 = text(ui_qr, "Rede", &lv_font_montserrat_12, QR_COLOR_MUTED);
    lv_obj_set_pos(k1, QR_INFO_X, QR_INFO_Y);
    ssid_lbl = text(ui_qr, "--", &lv_font_montserrat_14, QR_COLOR_TEXT);
    lv_obj_set_pos(ssid_lbl, QR_INFO_X, QR_INFO_Y + 14);

    lv_obj_t *k2 = text(ui_qr, "Senha", &lv_font_montserrat_12, QR_COLOR_MUTED);
    lv_obj_set_pos(k2, QR_INFO_X, QR_INFO_Y + 38);
    pass_lbl = text(ui_qr, "--", &lv_font_montserrat_14, QR_COLOR_TEXT);
    lv_obj_set_pos(pass_lbl, QR_INFO_X, QR_INFO_Y + 52);

    lv_obj_t *k3 = text(ui_qr, "Se nao abrir sozinho", &lv_font_montserrat_12, QR_COLOR_MUTED);
    lv_obj_set_pos(k3, QR_INFO_X, QR_INFO_Y + 76);
    url_lbl = text(ui_qr, "--", &lv_font_montserrat_12, QR_COLOR_TEXT);
    lv_obj_set_pos(url_lbl, QR_INFO_X, QR_INFO_Y + 90);

    hint_lbl = text(ui_qr, "", &lv_font_montserrat_12, QR_COLOR_MUTED);
    lv_obj_set_pos(hint_lbl, QR_PAD_X, QR_HINT_Y);
    lv_obj_set_width(hint_lbl, 320 - 2 * QR_PAD_X);
    lv_label_set_long_mode(hint_lbl, LV_LABEL_LONG_WRAP);

    ui_navigation_bind_pointer(ui_qr);

    drawn_once = false;
    drawn_qr[0] = '\0';
    refresh(true);
    timer = lv_timer_create(timer_cb, QR_REFRESH_MS, NULL);
}

void ui_qr_screen_destroy(void)
{
    if (timer) lv_timer_delete(timer);
    timer = NULL;
    if (ui_qr) lv_obj_delete(ui_qr);
    ui_qr = NULL;
    status_lbl = qrcode = ssid_lbl = pass_lbl = url_lbl = hint_lbl = NULL;
    drawn_once = false;
    drawn_qr[0] = '\0';
}

#endif /* UI_ENABLE_WEB */
