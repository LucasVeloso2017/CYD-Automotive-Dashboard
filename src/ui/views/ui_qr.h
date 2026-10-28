#ifndef UI_QR_H
#define UI_QR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Acesso pelo celular: QR code com a rede WiFi do Wemos D1 mini, que serve a
 * pagina com o resumo dos gauges, o grafico do historico e o apagamento de DTC.
 * Vem depois da tela de codigos de falha no swipe.
 *
 * O CYD nao tem WiFi ligado: a rede, a senha e quantos celulares estao
 * conectados chegam pelo fio, no anuncio periodico do Wemos (ver net/link.h).
 * Sem esse anuncio a tela avisa e esconde o QR. */
extern void ui_qr_screen_init(void);
extern void ui_qr_screen_destroy(void);
extern lv_obj_t *ui_qr;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
