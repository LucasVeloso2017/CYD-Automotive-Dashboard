#include "ui.h"
#include "ui_mock_data.h"
#include "../gateway/bt_gateway.h"
#include "../gateway/elm327_gateway.h"
#include "../gateway/vehicle_data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LOG_INTERVAL_MS    MOCK_LOG_INTERVAL_MS
#define BOOST_MAX_FLOOR    (-50.0f)

/* Suavizacao dos gauges: perseguicao exponencial ("chase"), a mesma do
 * ponteiro do TSI Display (tsidisplay.com, demo).
 *
 * A cada quadro o valor exibido vence uma fracao fixa da distancia ate o
 * lido: rapido quando a diferenca e grande, freando ao chegar (ease-out). Com
 * leituras a ~10 por segundo, a proxima chega com o numero ainda em
 * movimento, e ele nunca para em degrau.
 *
 * SMOOTH_ALPHA e a fracao por quadro a 60 fps. Para nao depender da taxa de
 * quadros (um quadro atrasado nao pode virar tranco), o passo usa o tempo
 * real do quadro: k = 1 - (1 - alpha)^(dt * 60). A 60 fps da exatamente
 * alpha; a 30 fps, o equivalente a dois quadros de 60. 0.12 da uma constante
 * de tempo de ~130 ms.
 *
 * A primeira leitura de cada sessao entra sem rampa (snap): subir de 0 ate a
 * temperatura da agua com o motor quente seria segundos de numero errado. */
#define SMOOTH_ALPHA       0.12f
#define SMOOTH_DT_MAX_S    0.05f    /* quadro muito atrasado (troca de tela): no maximo 3 quadros de passo */

typedef struct {
    float target;
    float shown;
    bool  primed;
} smooth_t;

static smooth_t sm_lambda, sm_water, sm_batt, sm_boost, sm_comb, sm_iat, sm_oil, sm_eth;

static uint32_t now_ms = 0;                 /* lv_tick do quadro corrente */
static uint32_t last_frame_ms = 0;
static float    step_k = SMOOTH_ALPHA;      /* fracao deste quadro, calculada uma vez por quadro */

static void smooth_set(smooth_t *s, float target)
{
    s->target = target;
    if (!s->primed) {
        s->shown = target;
        s->primed = true;
    }
}

static void smooth_step(smooth_t *s)
{
    if (!s->primed) return;
    s->shown += (s->target - s->shown) * step_k;
}

static void smooth_reset(smooth_t *s)
{
    memset(s, 0, sizeof(*s));
}

/* Fracao do quadro a partir do tempo real desde o anterior. */
static void smooth_frame(void)
{
    float dt = (last_frame_ms == 0) ? (1.0f / 60.0f) : (float)(now_ms - last_frame_ms) / 1000.0f;
    last_frame_ms = now_ms;
    if (dt > SMOOTH_DT_MAX_S) dt = SMOOTH_DT_MAX_S;
    step_k = 1.0f - powf(1.0f - SMOOTH_ALPHA, dt * 60.0f);
}

/* Cada rotulo guarda o ultimo texto escrito. Com a suavizacao, render_gauges
 * roda a 60 Hz em vez de so quando chega leitura nova, e lv_label_set_text
 * invalida a area do rotulo mesmo quando o texto e identico — seriam 14
 * redesenhos por quadro para nada. */
enum {
    LBL_LAMBDA_NANO = 0,
    LBL_TEMP_NANO,
    LBL_BAT_NANO,
    LBL_BOOST,
    LBL_COMB,
    LBL_IAT,
    LBL_OIL,
    LBL_LAMBDA_TRACK,
    LBL_BAT_TRACK,
    LBL_MAX_BOOST,
    LBL_MAX_COMB,
    LBL_MAX_IAT,
    LBL_MAX_OIL,
    LBL_MAX_BAT,
    LBL_COUNT
};

static char lbl_cache[LBL_COUNT][12];

static float max_boost = BOOST_MAX_FLOOR;
static float max_comb = 0;
static float max_iat = 0;
static float max_oil = 0;
static float max_lambda = 0;
static float max_batt = 0;

static bool session_active = false;
static uint32_t last_log_ms = 0;
static uint32_t last_rev = 0;

static void set_text(uint8_t slot, lv_obj_t *l, const char *s)
{
    if (!l) return;
    if (strcmp(lbl_cache[slot], s) == 0) return;
    lv_snprintf(lbl_cache[slot], sizeof(lbl_cache[slot]), "%s", s);
    lv_label_set_text(l, s);
}

static void fmt_f1(uint8_t slot, lv_obj_t *l, float v)
{
    if (!l) return;
    char b[12];
    int w = (int)v, f = (int)((v - w) * 10);
    if (f < 0) f = -f;
    if (v < 0 && w == 0) lv_snprintf(b, sizeof(b), "-%d.%d", w, f);
    else lv_snprintf(b, sizeof(b), "%d.%d", w, f);
    set_text(slot, l, b);
}

static void fmt_f2(uint8_t slot, lv_obj_t *l, float v)
{
    if (!l) return;
    char b[12];
    int w = (int)v, f = (int)((v - w) * 100);
    if (f < 0) f = -f;
    /* -0.05 tem parte inteira 0: sem isto o sinal sumia (vacuo virava pressao) */
    if (v < 0 && w == 0) lv_snprintf(b, sizeof(b), "-0.%02d", f);
    else lv_snprintf(b, sizeof(b), "%d.%02d", w, f);
    set_text(slot, l, b);
}

static void fmt_int(uint8_t slot, lv_obj_t *l, int v)
{
    if (!l) return;
    char b[12];
    lv_snprintf(b, sizeof(b), "%d", v);
    set_text(slot, l, b);
}

static void update_max_values(float boost, float comb, float iat, float oil,
                              float lambda, float batt)
{
    if (boost > max_boost) max_boost = boost;
    if (comb > max_comb) max_comb = comb;
    if (iat > max_iat) max_iat = iat;
    if (oil > max_oil) max_oil = oil;
    if (lambda > max_lambda) max_lambda = lambda;
    if (batt > max_batt) max_batt = batt;
}

/* Desenha o valor suavizado. Os maximos da sessao saem crus de proposito: pico
 * e um instante que aconteceu, nao uma grandeza que varia. */
static void render_gauges(void)
{
    /* Uma casa a mais que a resolucao "de leitura" em cada gauge: com a
     * interpolacao, o ultimo digito anda a cada quadro e o numero corre em vez
     * de pular. Os maximos seguem o mesmo formato do valor ao vivo. */
    fmt_f2(LBL_LAMBDA_NANO, ui_lambdanano, sm_lambda.shown);
    fmt_f1(LBL_TEMP_NANO, ui_temperaturenano, sm_water.shown);
    fmt_f2(LBL_BAT_NANO, ui_baterynano, sm_batt.shown);

    fmt_f2(LBL_BOOST, ui_boosttrack, sm_boost.shown);
    fmt_f2(LBL_COMB, ui_combpressuretrack, sm_comb.shown);
    fmt_f1(LBL_IAT, ui_iattrack, sm_iat.shown);
    fmt_f1(LBL_OIL, ui_oiltrack, sm_oil.shown);
    fmt_f2(LBL_LAMBDA_TRACK, ui_lambdatrack, sm_lambda.shown);
    fmt_f2(LBL_BAT_TRACK, ui_baterytrack, sm_batt.shown);

    fmt_f2(LBL_MAX_BOOST, ui_maxbostttrack1, max_boost <= BOOST_MAX_FLOOR ? 0.0f : max_boost);
    fmt_f2(LBL_MAX_COMB, ui_maxcombttrack, max_comb);
    fmt_f1(LBL_MAX_IAT, ui_maxiattrack1, max_iat);
    fmt_f1(LBL_MAX_OIL, ui_maxoiltrack, max_oil);
    fmt_f2(LBL_MAX_BAT, ui_maxbaterytrack1, max_batt);
}

static void reset_max_values(void)
{
    max_boost = BOOST_MAX_FLOOR;
    max_comb = 0;
    max_iat = 0;
    max_oil = 0;
    max_lambda = 0;
    max_batt = 0;
}

static void reset_smoothing(void)
{
    smooth_reset(&sm_lambda);
    smooth_reset(&sm_water);
    smooth_reset(&sm_batt);
    smooth_reset(&sm_boost);
    smooth_reset(&sm_comb);
    smooth_reset(&sm_iat);
    smooth_reset(&sm_oil);
    smooth_reset(&sm_eth);
}

static void update_default_values(void)
{
    static const uint8_t slots[] = {
        LBL_LAMBDA_NANO, LBL_TEMP_NANO, LBL_BAT_NANO,
        LBL_BOOST, LBL_COMB, LBL_IAT, LBL_OIL, LBL_LAMBDA_TRACK, LBL_BAT_TRACK,
        LBL_MAX_BOOST, LBL_MAX_COMB, LBL_MAX_IAT, LBL_MAX_OIL, LBL_MAX_BAT,
    };
    lv_obj_t *labels[] = {
        ui_lambdanano,
        ui_temperaturenano,
        ui_baterynano,
        ui_boosttrack,
        ui_combpressuretrack,
        ui_iattrack,
        ui_oiltrack,
        ui_lambdatrack,
        ui_baterytrack,
        ui_maxbostttrack1,
        ui_maxcombttrack,
        ui_maxiattrack1,
        ui_maxoiltrack,
        ui_maxbaterytrack1,
    };

    for (uint8_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        set_text(slots[i], labels[i], "00");
    }
}

static void log_gauges(uint32_t now)
{
    if (now - last_log_ms < LOG_INTERVAL_MS) return;
    last_log_ms = now;

    const vehicle_gauge_data_t *g = &vehicle.gauges;
    printf(
        "[OBD2] boost=%.2f max=%.2f | comb=%.2f max=%.2f | iat=%.0f max=%.0f | "
        "oil=%.0f max=%.0f | lambda=%.3f max=%.3f | batt=%.1f max=%.1f | cool=%.0f | eth=%.0f\n",
        g->boost_bar, max_boost <= BOOST_MAX_FLOOR ? 0.0f : max_boost,
        g->comb_pressure_bar, max_comb,
        g->iat_c, max_iat,
        g->oil_temp_c, max_oil,
        g->lambda, max_lambda,
        g->battery_v, max_batt,
        g->water_temp_c,
        g->ethanol_pct
    );
}

/* Valores do quadro: leitura nova, interpolacao e texto dos rotulos. */
static void update_gauges(void)
{
    bool connected = bt_is_connected() && elm327_is_ready();

    if (!connected) {
        if (session_active) {
            session_active = false;
            last_rev = 0;
            reset_max_values();
            reset_smoothing();
            printf("[OBD2] Session ended - max values reset\n");
        }
        update_default_values();
        ui_etanol_render(-1.0f, false);
        return;
    }

    if (!vehicle.gauges.valid) return;

    if (!session_active) {
        session_active = true;
        reset_max_values();
        reset_smoothing();
        printf("[OBD2] Session started\n");
    }

    const vehicle_gauge_data_t *g = &vehicle.gauges;

    /* Leitura nova: move os alvos, os maximos e o historico. O resto do tempo
     * so a rampa corre, a 60 Hz. */
    if (vehicle.gauges.data_rev != last_rev) {
        last_rev = vehicle.gauges.data_rev;

        smooth_set(&sm_lambda, g->lambda);
        smooth_set(&sm_water, g->water_temp_c);
        smooth_set(&sm_batt, g->battery_v);
        smooth_set(&sm_boost, g->boost_bar);
        smooth_set(&sm_comb, g->comb_pressure_bar);
        smooth_set(&sm_iat, g->iat_c);
        smooth_set(&sm_oil, g->oil_temp_c);
        /* Etanol so depois da primeira leitura: o -1 de "sem leitura" nao
         * pode virar o ponto de partida da rampa. */
        if (g->ethanol_pct >= 0.0f) smooth_set(&sm_eth, g->ethanol_pct);

        update_max_values(g->boost_bar, g->comb_pressure_bar, g->iat_c,
                          g->oil_temp_c, g->lambda, g->battery_v);
        log_gauges(lv_tick_get());
    }

    smooth_step(&sm_lambda);
    smooth_step(&sm_water);
    smooth_step(&sm_batt);
    smooth_step(&sm_boost);
    smooth_step(&sm_comb);
    smooth_step(&sm_iat);
    smooth_step(&sm_oil);
    smooth_step(&sm_eth);

    render_gauges();
    ui_etanol_render(sm_eth.primed ? sm_eth.shown : -1.0f, true);
}

/* Quadro garantido a cada ciclo. O LVGL so desenha o que foi invalidado, e
 * um rotulo com o mesmo texto nao se invalida: com tudo parado, ciclos
 * passavam sem quadro. Invalidar 1 pixel basta para o ciclo produzir um
 * quadro; os numeros que mudaram ja se invalidaram sozinhos no set_text.
 * (A versao anterior redesenhava todos os rotulos em todo ciclo, mudando ou
 * nao — era o custo maximo sempre, e o que segurava a taxa em ~37.) */
static void hold_frame(void)
{
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;
    lv_area_t px = { 0, 0, 0, 0 };
    lv_obj_invalidate_area(scr, &px);
}

/* Roda no comeco de cada ciclo de refresh do display (LV_EVENT_REFR_START),
 * antes de o LVGL juntar as areas sujas: valor e quadro andam travados, um
 * para um, no periodo do display (LVGL_REFR_MS no .ino). Um lv_timer proprio
 * de 16 ms corria solto ao lado do refresh e as vezes caia entre dois ciclos,
 * deixando um quadro sem valor novo. */
static void poll_cb(lv_event_t *e)
{
    (void)e;
    now_ms = lv_tick_get();
    smooth_frame();
    update_gauges();
    hold_frame();
}

void ui_mock_data_session_max(vehicle_gauge_data_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    /* Mesma convencao do painel: sem pico registrado o boost mostra 0, nao o
     * piso sentinela. */
    out->boost_bar         = (max_boost <= BOOST_MAX_FLOOR) ? 0.0f : max_boost;
    out->comb_pressure_bar = max_comb;
    out->iat_c             = max_iat;
    out->oil_temp_c        = max_oil;
    out->lambda            = max_lambda;
    out->battery_v         = max_batt;
    out->valid             = session_active;
}

void ui_mock_data_init(void)
{
    reset_max_values();
    reset_smoothing();
    session_active = false;
    last_log_ms = 0;
    last_rev = 0;
    memset(lbl_cache, 0, sizeof(lbl_cache));
    update_default_values();
    lv_display_add_event_cb(lv_display_get_default(), poll_cb, LV_EVENT_REFR_START, NULL);
}
