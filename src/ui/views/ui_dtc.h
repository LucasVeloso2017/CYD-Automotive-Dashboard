#ifndef UI_DTC_H
#define UI_DTC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Codigos de falha (DTC): ler, listar e apagar pelo ELM327. Vem depois da
 * blelist no swipe. O trabalho no barramento e da task do gateway; esta tela so
 * pede e mostra (elm327_request_dtc_read/clear, elm327_dtc_copy). */
extern void ui_dtc_screen_init(void);
extern void ui_dtc_screen_destroy(void);
extern lv_obj_t *ui_dtc;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
