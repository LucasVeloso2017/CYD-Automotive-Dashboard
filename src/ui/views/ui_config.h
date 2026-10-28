#ifndef UI_CONFIG_H
#define UI_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Configuracoes, ultima tela do swipe: brilho e rotacao de 180 graus. */
extern lv_obj_t *ui_config;

void ui_config_screen_init(void);
void ui_config_screen_destroy(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
