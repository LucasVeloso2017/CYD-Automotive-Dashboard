#ifndef UI_MOCK_DATA_H
#define UI_MOCK_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../gateway/vehicle_data.h"

#define MOCK_LOG_INTERVAL_MS   1000

void ui_mock_data_init(void);

/**
 * Maximos da sessao corrente, nos mesmos campos de vehicle_gauge_data_t.
 *
 * Os picos sao acumulados aqui, no mesmo tick que desenha os gauges, e o
 * link com o Wemos os manda para o resumo da pagina web. Ler daqui evita uma segunda
 * contabilidade que poderia divergir do que esta na tela.
 */
void ui_mock_data_session_max(vehicle_gauge_data_t *out);


#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
