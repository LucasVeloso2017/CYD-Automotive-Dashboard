#ifndef ELM327_GATEWAY_H
#define ELM327_GATEWAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dtc_data.h"

#define ELM327_CMD_TIMEOUT_MS  1200

void elm327_init_session(void);
void elm327_reset_session(void);
void elm327_loop(void);
bool elm327_is_ready(void);

/* DTCs sob demanda. Os pedidos sao atendidos pela task de polling, entre
 * duas trocas de PID (a serial e dela); leitura ~2 s, apagar + reler ~4 s.
 * O snapshot sobrevive ao disconnect. */
const dtc_snapshot_t *elm327_dtc(void);            /* ponteiro cru: so para o teste host */
void               elm327_dtc_copy(dtc_snapshot_t *out);   /* copia consistente para a UI */
void               elm327_request_dtc_read(void);
void               elm327_request_dtc_clear(void);         /* modo 04 e releitura */
dtc_op_t           elm327_dtc_op(void);
dtc_clear_result_t elm327_dtc_clear_result(uint8_t *nrc);  /* nrc pode ser NULL */

#ifdef __cplusplus
}
#endif

#endif
