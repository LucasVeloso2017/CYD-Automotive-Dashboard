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
void elm327_stop_task(void);   /* encerra a task de polling e espera sair (libera 8 KB) */

/* DTCs: lidos ao abrir a sessao e a cada 60 s pela task de polling; o
 * snapshot sobrevive ao disconnect (a pagina Wi-Fi le com o BT suspenso). */
const dtc_snapshot_t *elm327_dtc(void);
void elm327_request_dtc_read(void);

#ifdef __cplusplus
}
#endif

#endif
