#include "../ui.h"
#include "../ui_navigation.h"
#include "../../gateway/bt_gateway.h"
#include "../../gateway/elm327_gateway.h"
#include "../../gateway/dtc_describe.h"
#include <stdio.h>
#include <string.h>

/* Geometria e cores. O preview le todos os "#define DTC_*" por regex, entao
 * mantenha o formato "#define NOME valor". Textos em ASCII: as fontes
 * Montserrat do LVGL nao tem acentos. */
#define DTC_PAD_X             8
#define DTC_TITLE_Y           1
#define DTC_STATUS_Y          20
#define DTC_LIST_Y            38
#define DTC_LIST_H            160
#define DTC_BTN_Y             202
#define DTC_BTN_H             34
#define DTC_BTN_GAP           8
#define DTC_CARD_GAP          6
#define DTC_CARD_PAD          8
#define DTC_CARD_RADIUS       8
#define DTC_REFRESH_MS        300

#define DTC_COLOR_CARD        0x1C1C1C
#define DTC_COLOR_TEXT        0xFFFFFF
#define DTC_COLOR_MUTED       0x8B96A5
#define DTC_COLOR_STORED      0xFF4D4D
#define DTC_COLOR_PENDING     0xFFC233
#define DTC_COLOR_PERMANENT   0x8B96A5
#define DTC_COLOR_OK          0x2FD27A
#define DTC_COLOR_BTN_READ    0x2F6FDB
#define DTC_COLOR_BTN_CLEAR   0x9B1C1C
#define DTC_COLOR_BTN_OFF     0x333333

lv_obj_t *ui_dtc = NULL;

static lv_obj_t *status_lbl;
static lv_obj_t *list;
static lv_obj_t *btn_read, *btn_clear;
static lv_obj_t *mbox;
static lv_timer_t *timer;

/* O que esta desenhado: a lista so e reconstruida quando algo disto muda. */
static bool     drawn = false;
static bool     drawn_link;
static dtc_op_t drawn_op;
static uint32_t drawn_read_ms;
static bool     drawn_banner;

/* Resultado do apagamento fica visivel ate a proxima leitura manual. */
static bool show_clear_banner = false;

static bool link_up(void)
{
    return bt_is_connected() && elm327_is_ready();
}

/* ---------- helpers de estilo ---------- */

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    return l;
}

static lv_obj_t *card(lv_obj_t *parent, uint32_t border)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, lv_color_hex(DTC_COLOR_CARD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(c, lv_color_hex(border), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(c, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(c, DTC_CARD_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(c, DTC_CARD_PAD, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(c, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static void style_button(lv_obj_t *b, uint32_t color)
{
    lv_obj_set_style_bg_color(b, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(b, lv_color_hex(DTC_COLOR_BTN_OFF), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(b, lv_color_hex(DTC_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(b, lv_color_hex(DTC_COLOR_MUTED), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(b, DTC_CARD_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(b, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void set_enabled(lv_obj_t *b, bool on)
{
    if (on) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else    lv_obj_add_state(b, LV_STATE_DISABLED);
}

/* ---------- conteudo da lista ---------- */

static void hint(const char *s, uint32_t color)
{
    lv_obj_t *l = text(list, s, &lv_font_montserrat_14, color);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(l, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void code_card(uint16_t raw, const char *tag, uint32_t color)
{
    char code[6];
    dtc_format(raw, code);
    dtc_info_t info;
    dtc_describe(raw, &info);

    lv_obj_t *c = card(list, color);

    lv_obj_t *head = lv_obj_create(c);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *code_lbl = text(head, code, &lv_font_montserrat_20, DTC_COLOR_TEXT);
    lv_obj_align(code_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *tag_lbl = text(head, tag, &lv_font_montserrat_12, color);
    lv_obj_align(tag_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    char desc[160];
    int n = snprintf(desc, sizeof(desc), "%s - %s", info.system, info.generic ? "generico SAE" : "do fabricante");
    if (info.subsystem && n < (int)sizeof(desc))
        n += snprintf(desc + n, sizeof(desc) - n, "\n%s", info.subsystem);
    if (info.cylinder > 0 && n < (int)sizeof(desc))
        snprintf(desc + n, sizeof(desc) - n, ", cilindro %d", info.cylinder);
    else if (info.cylinder < 0 && n < (int)sizeof(desc))
        snprintf(desc + n, sizeof(desc) - n, ", aleatoria ou em varios cilindros");

    lv_obj_t *d = text(c, desc, &lv_font_montserrat_12, DTC_COLOR_MUTED);
    lv_obj_set_width(d, lv_pct(100));
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
}

static void freeze_frame_card(const dtc_snapshot_t *s)
{
    char code[6], buf[200];
    dtc_format(s->ff.dtc, code);
    snprintf(buf, sizeof(buf),
             "Freeze frame de %s\n%u rpm   %u km/h   agua %d C\ncarga %u%%   borboleta %u%%   MAP %u kPa",
             code, s->ff.rpm, s->ff.speed_kmh, s->ff.coolant_c, s->ff.load_pct, s->ff.throttle_pct, s->ff.map_kpa);
    lv_obj_t *c = card(list, DTC_COLOR_MUTED);
    lv_obj_set_style_border_opa(c, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *l = text(c, buf, &lv_font_montserrat_12, DTC_COLOR_MUTED);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
}

static void clear_banner(const dtc_snapshot_t *s)
{
    uint8_t nrc = 0;
    dtc_clear_result_t r = elm327_dtc_clear_result(&nrc);
    if (r == DTC_CLEAR_NONE) return;

    char buf[128];
    uint32_t color;
    if (r == DTC_CLEAR_OK && s->valid && s->n_stored > 0) {
        /* A ECU aceitou o 04, mas a releitura (1,5 s depois) ja encontrou o
         * codigo de novo: a falha continua presente e a ECU o gravou outra vez.
         * Sem isto a tela dizia "apagados" em cima da lista cheia. */
        snprintf(buf, sizeof(buf), "Apagados, mas %u voltou: a falha ainda esta presente no carro.",
                 (unsigned)s->n_stored);
        color = DTC_COLOR_PENDING;
    } else if (r == DTC_CLEAR_OK) {
        if (s->valid && s->n_permanent > 0)
            snprintf(buf, sizeof(buf), "Codigos apagados. Os permanentes so saem depois que o carro confirmar o reparo.");
        else
            snprintf(buf, sizeof(buf), "Codigos apagados.");
        color = DTC_COLOR_OK;
    } else if (r == DTC_CLEAR_REFUSED) {
        if (nrc == 0x22) snprintf(buf, sizeof(buf), "A ECU recusou: condicoes nao atendidas. Desligue o motor e mantenha a ignicao ligada.");
        else             snprintf(buf, sizeof(buf), "A ECU recusou o apagamento (codigo 0x%02X).", nrc);
        color = DTC_COLOR_PENDING;
    } else {
        snprintf(buf, sizeof(buf), "Sem resposta da ECU ao apagar.");
        color = DTC_COLOR_STORED;
    }
    lv_obj_t *c = card(list, color);
    lv_obj_t *l = text(c, buf, &lv_font_montserrat_14, color);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
}

static void rebuild_list(bool link, dtc_op_t op, const dtc_snapshot_t *s)
{
    lv_obj_clean(list);

    if (!bt_is_connected()) {
        hint("Conecte o ELM327 na tela Bluetooth para ler os codigos.", DTC_COLOR_MUTED);
    } else if (!link) {
        hint("Aguardando o ELM327 responder...", DTC_COLOR_MUTED);
    } else if (op != DTC_OP_IDLE) {
        lv_obj_t *sp = lv_spinner_create(list);
        lv_obj_set_size(sp, 44, 44);
        lv_obj_set_style_margin_top(sp, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_color(sp, lv_color_hex(DTC_COLOR_BTN_READ), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_color(sp, lv_color_hex(DTC_COLOR_CARD), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_flag(sp, LV_OBJ_FLAG_CLICKABLE);
        hint(op == DTC_OP_CLEARING ? "Apagando os codigos..." : "Lendo os codigos...", DTC_COLOR_MUTED);
        lv_obj_set_style_pad_top(lv_obj_get_child(list, -1), 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (!s->valid) {
        hint("Toque em Ler para buscar os codigos.", DTC_COLOR_MUTED);
    } else {
        if (show_clear_banner) clear_banner(s);

        if (s->n_stored + s->n_pending + s->n_permanent == 0) {
            hint("Nenhum codigo de falha.", DTC_COLOR_OK);
        } else {
            for (uint8_t i = 0; i < s->n_stored; i++) code_card(s->stored[i], "ARMAZENADO", DTC_COLOR_STORED);
            if (s->ff.valid) freeze_frame_card(s);
            for (uint8_t i = 0; i < s->n_pending; i++) code_card(s->pending[i], "PENDENTE", DTC_COLOR_PENDING);
            for (uint8_t i = 0; i < s->n_permanent; i++) code_card(s->permanent[i], "PERMANENTE", DTC_COLOR_PERMANENT);
        }
    }

    /* filhos novos precisam repassar press/release, senao o swipe morre neles */
    ui_navigation_apply_bubble(ui_dtc);
}

/* ---------- linha de estado ---------- */

static void update_status(bool link, dtc_op_t op, const dtc_snapshot_t *s)
{
    char buf[80];
    uint32_t color = DTC_COLOR_MUTED;

    if (!bt_is_connected())       snprintf(buf, sizeof(buf), "Sem conexao com o ELM327");
    else if (!link)               snprintf(buf, sizeof(buf), "Inicializando o ELM327...");
    else if (op != DTC_OP_IDLE)   snprintf(buf, sizeof(buf), "%s", op == DTC_OP_CLEARING ? "Apagando..." : "Lendo...");
    else if (!s->valid)           snprintf(buf, sizeof(buf), "Ainda nao lido");
    else {
        uint32_t age = (lv_tick_get() - s->read_ms) / 1000;
        char ago[24];
        if (age < 60)        snprintf(ago, sizeof(ago), "%lu s", (unsigned long)age);
        else if (age < 3600) snprintf(ago, sizeof(ago), "%lu min", (unsigned long)(age / 60));
        else                 snprintf(ago, sizeof(ago), "%lu h", (unsigned long)(age / 3600));
        unsigned total = s->n_stored + s->n_pending + s->n_permanent;
        snprintf(buf, sizeof(buf), "MIL %s  -  %u codigo%s  -  lido ha %s",
                 s->mil ? "ACESA" : "apagada", total, total == 1 ? "" : "s", ago);
        color = s->mil ? DTC_COLOR_STORED : DTC_COLOR_OK;
    }
    lv_label_set_text(status_lbl, buf);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void refresh(bool force)
{
    if (!ui_dtc) return;
    bool link = link_up();
    dtc_op_t op = elm327_dtc_op();
    dtc_snapshot_t s;
    elm327_dtc_copy(&s);

    update_status(link, op, &s);

    bool can_act = link && op == DTC_OP_IDLE;
    set_enabled(btn_read, can_act);
    /* apagar exige ter lido e haver o que apagar: evita zerar os monitores de
     * prontidao por engano. Permanentes nao saem com o modo 04. */
    set_enabled(btn_clear, can_act && s.valid && (s.n_stored + s.n_pending) > 0);

    if (force || !drawn || link != drawn_link || op != drawn_op ||
        s.read_ms != drawn_read_ms || show_clear_banner != drawn_banner) {
        rebuild_list(link, op, &s);
        drawn = true;
        drawn_link = link;
        drawn_op = op;
        drawn_read_ms = s.read_ms;
        drawn_banner = show_clear_banner;
    }

    /* conexao caiu com a confirmacao aberta: fecha, o pedido nao teria efeito */
    if (mbox && !link) {
        lv_msgbox_close(mbox);
        mbox = NULL;
    }
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_screen_active() != ui_dtc) return;   /* fora da tela nao ha o que desenhar */
    refresh(false);
}

/* ---------- eventos ---------- */

/* Abrir a tela so desenha o que ja existe: a leitura de DTC acontece apenas
 * quando o usuario toca em Ler. Cada leitura tira o polling dos gauges do ar
 * por ~2 s, e passar pela tela no swipe nao e pedir isso. */
static void screen_loaded_cb(lv_event_t *e)
{
    (void)e;
    refresh(true);
}

static void read_click_cb(lv_event_t *e)
{
    (void)e;
    if (ui_navigation_swipe_consumed()) return;
    show_clear_banner = false;
    elm327_request_dtc_read();
    refresh(false);
}

static void confirm_click_cb(lv_event_t *e)
{
    bool do_clear = (bool)(uintptr_t)lv_event_get_user_data(e);
    if (mbox) {
        lv_msgbox_close_async(mbox);   /* estamos dentro do evento de um filho dele */
        mbox = NULL;
    }
    if (do_clear) {
        show_clear_banner = true;
        elm327_request_dtc_clear();
    }
    refresh(false);
}

static void clear_click_cb(lv_event_t *e)
{
    (void)e;
    if (ui_navigation_swipe_consumed() || mbox) return;

    /* modal: com parent NULL o LVGL cria um fundo na lv_layer_top que bloqueia
     * o toque (e o swipe) ate uma escolha */
    mbox = lv_msgbox_create(NULL);
    lv_obj_set_width(mbox, 290);
    lv_obj_set_style_bg_color(mbox, lv_color_hex(DTC_COLOR_CARD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(mbox, lv_color_hex(DTC_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(mbox, lv_color_hex(DTC_COLOR_BTN_CLEAR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(mbox, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(mbox, DTC_CARD_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_msgbox_add_title(mbox, "Apagar os codigos?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(DTC_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_msgbox_get_header(mbox), LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *body = lv_msgbox_add_text(mbox,
        "Tambem zera o freeze frame e os monitores de prontidao (o carro leva alguns ciclos "
        "para refaze-los). Faca com a ignicao ligada e o motor desligado. "
        "Codigos permanentes nao saem.");
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(body, lv_color_hex(DTC_COLOR_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *no = lv_msgbox_add_footer_button(mbox, "Cancelar");   /* cria o footer */
    lv_obj_set_style_bg_opa(lv_msgbox_get_footer(mbox), LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    style_button(no, DTC_COLOR_BTN_OFF);
    lv_obj_add_event_cb(no, confirm_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)false);
    lv_obj_t *yes = lv_msgbox_add_footer_button(mbox, "Apagar");
    style_button(yes, DTC_COLOR_BTN_CLEAR);
    lv_obj_add_event_cb(yes, confirm_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)true);
}

/* ---------- init / destroy ---------- */

static lv_obj_t *button(const char *label, uint32_t color, int32_t x, int32_t w, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(ui_dtc);
    lv_obj_set_pos(b, x, DTC_BTN_Y);
    lv_obj_set_size(b, w, DTC_BTN_H);
    style_button(b, color);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_center(l);
    return b;
}

void ui_dtc_screen_init(void)
{
    ui_dtc = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_dtc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_dtc, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_dtc, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_dtc, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

    lv_obj_t *title = text(ui_dtc, "Codigos de falha", &lv_font_montserrat_16, DTC_COLOR_TEXT);
    lv_obj_set_pos(title, DTC_PAD_X, DTC_TITLE_Y);

    status_lbl = text(ui_dtc, "", &lv_font_montserrat_12, DTC_COLOR_MUTED);
    lv_obj_set_pos(status_lbl, DTC_PAD_X, DTC_STATUS_Y);
    lv_obj_set_width(status_lbl, 320 - 2 * DTC_PAD_X);
    lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_DOT);

    /* lista rolavel na vertical; o horizontal fica para o swipe entre telas */
    list = lv_obj_create(ui_dtc);
    lv_obj_set_pos(list, DTC_PAD_X, DTC_LIST_Y);
    lv_obj_set_size(list, 320 - 2 * DTC_PAD_X, DTC_LIST_H);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(list, DTC_CARD_GAP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    int32_t bw = (320 - 2 * DTC_PAD_X - DTC_BTN_GAP) / 2;
    btn_read  = button("Ler",    DTC_COLOR_BTN_READ,  DTC_PAD_X, bw, read_click_cb);
    btn_clear = button("Apagar", DTC_COLOR_BTN_CLEAR, DTC_PAD_X + bw + DTC_BTN_GAP, bw, clear_click_cb);

    ui_navigation_bind_pointer(list);
    ui_navigation_bind_pointer(ui_dtc);

    drawn = false;
    refresh(true);
    timer = lv_timer_create(timer_cb, DTC_REFRESH_MS, NULL);
}

void ui_dtc_screen_destroy(void)
{
    if (timer) lv_timer_delete(timer);
    timer = NULL;
    if (mbox) lv_msgbox_close(mbox);
    mbox = NULL;
    if (ui_dtc) lv_obj_delete(ui_dtc);
    ui_dtc = NULL;
    status_lbl = list = btn_read = btn_clear = NULL;
    drawn = false;
}
