#include "history.h"

#include <math.h>
#include <stddef.h>

/* Num ESP32 o buffer iria para a memoria RTC lenta (ver history.h); NOINIT
 * porque history_reset() zera a contagem antes de qualquer leitura. No ESP8266
 * do Wemos e no PC (testes) e um array comum. */
#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define HISTORY_STORAGE RTC_NOINIT_ATTR
#else
#define HISTORY_STORAGE
#endif

enum {
    F_LAMBDA = 0,
    F_WATER,
    F_BATT,
    F_BOOST,
    F_COMB,
    F_IAT,
    F_OIL,
    F_COUNT
};

/* Escala por grandeza: valor guardado = round(real x escala). Cada uma cobre a
 * faixa fisica com folga dentro de +-32767 e guarda pelo menos a precisao que a
 * pagina mostra (lambda 3 casas, boost 2, bateria 1, rampa 1, temperaturas 0). */
static const float SCALE[F_COUNT] = {
    1000.0f,   /* lambda      0.5..2.0      -> 500..2000            */
    10.0f,     /* agua        -40..215 C                              */
    100.0f,    /* bateria     0..65 V                                 */
    1000.0f,   /* boost       -1..+3 bar                              */
    10.0f,     /* rampa       0..3276 (unidade do gauge)              */
    10.0f,     /* admissao    -40..215 C                              */
    10.0f,     /* oleo        -40..215 C                              */
};

#define NO_READING  INT16_MIN      /* NaN: grandeza sem leitura */

typedef struct {
    int16_t v[F_COUNT];
} packed_t;

static HISTORY_STORAGE packed_t _buf[HISTORY_CAP];
static uint16_t _count = 0;     /* 0..HISTORY_CAP */
static uint16_t _head  = 0;     /* proxima posicao a escrever */
static uint32_t _last_ms = 0;
static bool     _primed = false;   /* ja houve uma primeira amostra? */

static int16_t pack(float v, float scale)
{
    if (!isfinite(v)) return NO_READING;
    float s = v * scale;
    /* Satura em vez de dar a volta: um valor fora da faixa vira o extremo, nao
     * um numero do lado oposto do grafico. */
    if (s >  32767.0f) return 32767;
    if (s < -32767.0f) return -32767;
    return (int16_t)lroundf(s);
}

static float unpack(int16_t v, float scale)
{
    if (v == NO_READING) return NAN;
    return (float)v / scale;
}

void history_reset(void)
{
    _count = 0;
    _head = 0;
    _last_ms = 0;
    _primed = false;
}

bool history_tick(const history_sample_t *g, uint32_t now_ms)
{
    if (!g) return false;

    /* A primeira amostra entra na hora; as seguintes respeitam o intervalo.
     * A subtracao em uint32_t trata o rollover do relogio sozinha. */
    if (_primed && (uint32_t)(now_ms - _last_ms) < HISTORY_INTERVAL_MS) return false;

    packed_t *p = &_buf[_head];
    p->v[F_LAMBDA] = pack(g->lambda,            SCALE[F_LAMBDA]);
    p->v[F_WATER]  = pack(g->water_temp_c,      SCALE[F_WATER]);
    p->v[F_BATT]   = pack(g->battery_v,         SCALE[F_BATT]);
    p->v[F_BOOST]  = pack(g->boost_bar,         SCALE[F_BOOST]);
    p->v[F_COMB]   = pack(g->comb_pressure_bar, SCALE[F_COMB]);
    p->v[F_IAT]    = pack(g->iat_c,             SCALE[F_IAT]);
    p->v[F_OIL]    = pack(g->oil_temp_c,        SCALE[F_OIL]);

    _head = (uint16_t)((_head + 1) % HISTORY_CAP);
    if (_count < HISTORY_CAP) _count++;
    _last_ms = now_ms;
    _primed = true;
    return true;
}

uint16_t history_count(void)
{
    return _count;
}

bool history_get(uint16_t i, history_sample_t *out)
{
    if (!out || i >= _count) return false;

    /* Com o buffer cheio, _head aponta para a mais antiga; antes disso o
     * buffer ainda e linear e a mais antiga esta no indice 0. */
    const uint16_t oldest = (_count == HISTORY_CAP) ? _head : 0;
    const packed_t *p = &_buf[(uint16_t)((oldest + i) % HISTORY_CAP)];

    out->lambda            = unpack(p->v[F_LAMBDA], SCALE[F_LAMBDA]);
    out->water_temp_c      = unpack(p->v[F_WATER],  SCALE[F_WATER]);
    out->battery_v         = unpack(p->v[F_BATT],   SCALE[F_BATT]);
    out->boost_bar         = unpack(p->v[F_BOOST],  SCALE[F_BOOST]);
    out->comb_pressure_bar = unpack(p->v[F_COMB],   SCALE[F_COMB]);
    out->iat_c             = unpack(p->v[F_IAT],    SCALE[F_IAT]);
    out->oil_temp_c        = unpack(p->v[F_OIL],    SCALE[F_OIL]);
    return true;
}
