#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Brilho e rotacao do display do CYD, guardados na NVS.
 *
 * O brilho e PWM no pino do backlight (TFT_BL, GPIO 21). A rotacao vira
 * painel e toque juntos em 180 graus (1 <-> 3), para quem monta o CYD de
 * cabeca para baixo no carro. */

#define DISPLAY_BRIGHTNESS_MIN  5     /* %: abaixo disso a tela parece desligada */
#define DISPLAY_BRIGHTNESS_MAX  100

/** Le a NVS e aplica a rotacao. Chame depois de tft.begin() e ts.begin(),
 *  antes do primeiro quadro; o backlight fica apagado. */
void    display_init(void);

/** Acende o backlight no brilho salvo (fim do setup). */
void    display_backlight_on(void);

uint8_t display_brightness(void);
/** Aplica na hora, sem gravar: o slider chama isto a cada passo. */
void    display_set_brightness(uint8_t pct);
/** Grava o brilho atual na NVS (no fim do arraste, para nao gastar a flash). */
void    display_save_brightness(void);

bool    display_flipped(void);
/** Gira painel e toque 180 graus e grava. O LVGL precisa redesenhar a tela
 *  inteira depois (o painel ja tem o quadro antigo na posicao antiga). */
void    display_set_flipped(bool flipped);

#ifdef __cplusplus
}
#endif

#endif
