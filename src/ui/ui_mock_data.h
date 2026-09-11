#ifndef UI_MOCK_DATA_H
#define UI_MOCK_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define MOCK_POLL_INTERVAL_MS  16
#define MOCK_LOG_INTERVAL_MS   1000

/* Resumo da sessao atual (do connect ao disconnect do ELM327). E o que a
 * pagina web do track-metrics consome; os gauges continuam lendo os max
 * direto dos estaticos deste modulo. */
typedef struct {
    bool     active;
    uint32_t start_ms;      /* lv_tick_get() no inicio da sessao */
    uint32_t end_ms;        /* lv_tick_get() no fim; igual a start_ms enquanto ativa */
    uint32_t samples;       /* leituras aplicadas nesta sessao (0 = nunca houve) */
    float    max_boost, max_comb, max_iat, max_oil, max_lambda, max_batt, max_water;
    float    min_lambda, min_batt;
} ui_session_stats_t;

void ui_mock_data_init(void);
void ui_mock_data_refresh(void);   /* repinta os labels existentes com o estado atual */
void ui_session_get_stats(ui_session_stats_t *out);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
