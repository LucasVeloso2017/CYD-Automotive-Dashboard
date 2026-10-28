#ifndef UI_ETANOL_H
#define UI_ETANOL_H

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *ui_etanol;

void ui_etanol_screen_init(void);
void ui_etanol_screen_destroy(void);

/**
 * Desenha o percentual de etanol (PID 0x52).
 *
 * Chamado pelo tick dos gauges (ui_mock_data.c) com o valor ja suavizado.
 *
 * @param pct        0..100; negativo = carro ainda nao publicou.
 * @param connected  ELM327 pronto; sem ele a tela volta ao estado vazio.
 */
void ui_etanol_render(float pct, bool connected);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
