#ifndef UI_AUTOCONNECT_H
#define UI_AUTOCONNECT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Loading do boot: busca o dongle OBD2 pelo nome e conecta sozinho.
 *
 * Uma camada por cima de tudo (lv_layer_top) com spinner, por no maximo
 * AUTOCONNECT_WINDOW_MS. Achou: conecta e a camada sai. Nao achou: a camada
 * sai do mesmo jeito e o usuario escolhe na lista Bluetooth. */
void ui_autoconnect_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
