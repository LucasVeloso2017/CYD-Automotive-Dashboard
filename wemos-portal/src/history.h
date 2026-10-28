#ifndef HISTORY_H
#define HISTORY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Historico da sessao: o que a ECU leu, amostrado em intervalo fixo, para o
 * grafico da pagina web.
 *
 * Vive no Wemos, que recebe uma amostra do CYD a cada ~500 ms pela serial e
 * guarda uma a cada HISTORY_INTERVAL_MS. Tudo na loop(), sem trava.
 *
 * Compactado: int16 em escala fixa por grandeza, 14 B por amostra em vez de 32
 * — 4200 B para 10 minutos. A precisao guardada e igual ou maior que a que a
 * pagina exibe. (Num ESP32 o buffer iria para a memoria RTC; no ESP8266 do
 * Wemos fica na DRAM, que aqui nao disputa com Bluetooth nenhum.)
 */

#define HISTORY_CAP          300     /* amostras guardadas */
#define HISTORY_INTERVAL_MS  2000    /* 300 x 2 s = 10 min de janela */

/** Uma amostra ja decodificada. Grandeza sem leitura (NaN) volta como NaN. */
typedef struct {
    float lambda;
    float water_temp_c;
    float battery_v;
    float boost_bar;
    float comb_pressure_bar;
    float iat_c;
    float oil_temp_c;
} history_sample_t;

/** Esvazia o historico. Chamado quando uma sessao comeca ou termina. */
void history_reset(void);

/**
 * Guarda uma amostra se ja passou HISTORY_INTERVAL_MS desde a ultima.
 *
 * Com o buffer cheio, a amostra mais antiga sai. Quem chama so entrega leitura
 * valida: sem sessao no CYD nao ha ponto no grafico.
 *
 * @param s       Leitura corrente (NaN = grandeza sem leitura).
 * @param now_ms  Relogio em ms.
 * @return `true` quando a amostra entrou.
 */
bool history_tick(const history_sample_t *s, uint32_t now_ms);

/** Quantas amostras existem (0..HISTORY_CAP). */
uint16_t history_count(void);

/**
 * Amostra por indice cronologico, decodificada.
 *
 * @param i    0 e a mais antiga guardada, history_count()-1 a mais recente.
 * @param out  Destino.
 * @return `false` quando `i` esta fora da faixa ou `out` e nulo.
 */
bool history_get(uint16_t i, history_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif
