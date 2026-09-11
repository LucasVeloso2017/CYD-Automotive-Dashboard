#ifndef ELM327_GATEWAY_H
#define ELM327_GATEWAY_H

#ifdef __cplusplus
extern "C" {
#endif

#define ELM327_CMD_TIMEOUT_MS  1200

void elm327_init_session(void);
void elm327_reset_session(void);
void elm327_loop(void);
bool elm327_is_ready(void);
void elm327_stop_task(void);   /* encerra a task de polling e espera sair (libera 8 KB) */

#ifdef __cplusplus
}
#endif

#endif
