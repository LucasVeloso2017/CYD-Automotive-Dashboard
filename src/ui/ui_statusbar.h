#ifndef UI_STATUSBAR_H
#define UI_STATUSBAR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Barra de status no canto superior direito, na lv_layer_top(): uma so
 * instancia, desenhada por cima de qualquer tela e sem interceptar o toque.
 * Mostra so o icone de Bluetooth, enquanto o ELM327 estiver conectado.
 * Esconde-se sozinha enquanto o splash esta na tela. */
void ui_statusbar_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
