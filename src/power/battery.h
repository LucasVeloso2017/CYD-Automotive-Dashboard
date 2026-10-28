#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include <stdint.h>

/* Monitor da bateria Li-ion (1S, 3,7 V nominal) carregada pelo TP4056.
 *
 * Ligacao:  B+ ----[R1 100k]----+----[R2 100k]---- GND
 *                               |
 *                          GPIO 35 (ADC1_CH7, so entrada)   + 100 nF para GND
 *
 * O divisor por 2 leva os 4,2 V de bateria cheia para 2,1 V, dentro da faixa
 * linear do ADC com atenuacao de 11 dB. GPIO 35 e ADC1: continua funcionando
 * com Wi-Fi ligado (ADC2 nao). Corrente do divisor: ~21 uA, irrelevante.
 *
 * Aviso de alimentacao: a saida B+ do TP4056 e a tensao da bateria (3,0-4,2 V).
 * O CYD regula 3,3 V a partir do trilho de 5 V com um AMS1117 (queda ~1,1 V),
 * entao a bateria NAO pode ir direto no USB-C/VIN: precisa de um boost para
 * 5 V (MT3608, ou modulo TP4056 com saida boost integrada). */

#define BATTERY_ADC_PIN         35
#define BATTERY_DIVIDER_RATIO   2.0f      /* (R1 + R2) / R2 */
#define BATTERY_SAMPLE_MS       1000
#define BATTERY_SAMPLES         16        /* leituras por amostra, para tirar o ruido do ADC */
#define BATTERY_EMA_ALPHA       0.25f     /* filtro: 0 = congela, 1 = sem filtro */
#define BATTERY_CAL             1.000f    /* corrige a tolerancia dos resistores: tensao do multimetro / tensao lida */

/* Sem o divisor o GPIO 35 flutua (34-39 nao tem pull interno) e le ruido
 * perto de 0 V. Uma Li-ion presente nunca poe menos de ~1250 mV no pino (2,5 V
 * de corte da protecao / 2), entao ha folga para uma histerese longe do ruido:
 * sensor aparece acima de ON e so some abaixo de OFF. Ausente, a barra mostra
 * "--" em vez de um 0% que nao existe. */
#define BATTERY_SENSE_ON_PIN_MV  1000      /* celula >= 2,0 V */
#define BATTERY_SENSE_OFF_PIN_MV 600       /* celula <  1,2 V */
#define BATTERY_LOG_MS          10000     /* log [BAT] para calibrar na bancada; 0 desliga */

#ifdef __cplusplus
extern "C" {
#endif

void     battery_init(void);
void     battery_loop(void);          /* chamar a cada volta do loop(); amostra a cada BATTERY_SAMPLE_MS */
bool     battery_valid(void);         /* divisor presente e ja com leitura; falso = mostrar "--" */
uint16_t battery_millivolts(void);    /* tensao filtrada da bateria */
uint8_t  battery_percent(void);       /* 0..100 pela curva de descarga Li-ion */

#ifdef __cplusplus
}
#endif

#endif
