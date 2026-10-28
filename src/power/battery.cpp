#include "battery.h"

#include <Arduino.h>

static bool     _valid = false;
static bool     _sampled = false;     /* ja houve ao menos uma leitura */
static float    _mv_filtered = 0.0f;
static uint16_t _pin_mv = 0;          /* ultima leitura bruta no pino, antes do divisor */
static uint32_t _last_sample_ms = 0;
static uint32_t _last_log_ms = 0;

/* Tensao em circuito aberto x carga de uma celula Li-ion tipica (3,7 V
 * nominal). A curva e achatada no meio e despenca abaixo de 3,5 V, entao uma
 * reta 3,0-4,2 V erraria feio: marcaria 60% quando restam 20%. Interpolacao
 * linear entre os pontos. Sob carga a tensao afunda ~50-100 mV, o que faz o
 * percentual ler um pouco abaixo do real -- erra para o lado conservador. */
static const struct { uint16_t mv; uint8_t pct; } OCV_CURVE[] = {
    { 4200, 100 }, { 4100, 92 }, { 4000, 81 }, { 3920, 70 }, { 3850, 58 },
    { 3790, 46 },  { 3740, 34 }, { 3690, 22 }, { 3620, 12 }, { 3550, 6 },
    { 3450, 3 },   { 3300, 1 },  { 3000, 0 },
};
static const size_t OCV_POINTS = sizeof(OCV_CURVE) / sizeof(OCV_CURVE[0]);

static uint8_t mv_to_percent(uint16_t mv)
{
    if (mv >= OCV_CURVE[0].mv) return 100;
    if (mv <= OCV_CURVE[OCV_POINTS - 1].mv) return 0;
    for (size_t i = 1; i < OCV_POINTS; i++) {
        if (mv >= OCV_CURVE[i].mv) {
            const uint16_t mv_hi = OCV_CURVE[i - 1].mv, mv_lo = OCV_CURVE[i].mv;
            const uint8_t  p_hi  = OCV_CURVE[i - 1].pct, p_lo = OCV_CURVE[i].pct;
            return (uint8_t)(p_lo + (uint32_t)(mv - mv_lo) * (p_hi - p_lo) / (mv_hi - mv_lo));
        }
    }
    return 0;
}

static uint16_t read_pin_mv(void)
{
    uint32_t acc = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
        acc += analogReadMilliVolts(BATTERY_ADC_PIN);   /* ja aplica a calibracao de eFuse */
    }
    return (uint16_t)(acc / BATTERY_SAMPLES);
}

/* Le o pino, decide se ha sensor e atualiza o filtro. Detecta o divisor sendo
 * ligado ou desligado com a placa em funcionamento. */
static void sample(void)
{
    _pin_mv = read_pin_mv();
    bool present = _valid ? _pin_mv >= BATTERY_SENSE_OFF_PIN_MV     /* histerese: ruido nao faz piscar */
                          : _pin_mv >= BATTERY_SENSE_ON_PIN_MV;
    bool changed = !_sampled || present != _valid;

    if (changed) {
        if (present) Serial.printf("[BAT] sensor detectado no GPIO %d\n", BATTERY_ADC_PIN);
        else         Serial.printf("[BAT] sensor ausente: monte o divisor 100k/100k no GPIO %d (le %u mV no pino)\n",
                                   BATTERY_ADC_PIN, _pin_mv);
    }

    float cell_mv = (float)_pin_mv * BATTERY_DIVIDER_RATIO * BATTERY_CAL;
    if (present && changed) _mv_filtered = cell_mv;      /* primeira leitura valida: parte dela, nao de 0 */
    else if (present)       _mv_filtered += BATTERY_EMA_ALPHA * (cell_mv - _mv_filtered);
    _valid = present;
    _sampled = true;
}

void battery_init(void)
{
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
    pinMode(BATTERY_ADC_PIN, INPUT);

    sample();
    _last_sample_ms = _last_log_ms = millis();
    if (_valid) Serial.printf("[BAT] %u mV (%u%%)\n", battery_millivolts(), battery_percent());
}

void battery_loop(void)
{
    uint32_t now = millis();
    if (now - _last_sample_ms < BATTERY_SAMPLE_MS) return;
    _last_sample_ms = now;
    sample();

    if (BATTERY_LOG_MS && _valid && now - _last_log_ms >= BATTERY_LOG_MS) {
        _last_log_ms = now;
        Serial.printf("[BAT] pino %u mV -> celula %u mV -> %u%%\n",
                      _pin_mv, battery_millivolts(), battery_percent());
    }
}

bool battery_valid(void)
{
    return _valid;
}

uint16_t battery_millivolts(void)
{
    return (uint16_t)(_mv_filtered + 0.5f);
}

uint8_t battery_percent(void)
{
    return _valid ? mv_to_percent(battery_millivolts()) : 0;
}
