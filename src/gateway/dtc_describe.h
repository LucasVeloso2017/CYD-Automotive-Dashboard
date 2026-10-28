#ifndef DTC_DESCRIBE_H
#define DTC_DESCRIBE_H

#include <stdbool.h>
#include <stdint.h>

/* Decodificacao estrutural de um DTC pelo SAE J2012, sem tabela de textos: o
 * ELM327 so entrega o codigo, e uma tabela de descricoes custaria 40-60 KB.
 * Textos em ASCII porque as fontes Montserrat do LVGL nao tem acentos.
 *
 *   P0301 -> Motor e transmissao | generico SAE | Ignicao e falha de combustao | cilindro 1 */

typedef struct {
    const char *system;      /* sempre preenchido */
    bool        generic;     /* codigo SAE (mesmo significado em qualquer carro) ou do fabricante */
    const char *subsystem;   /* NULL quando o grupo so faz sentido com a tabela do fabricante */
    int8_t      cylinder;    /* 1..12; -1 = P0300 (aleatorio/multiplos); 0 = nao se aplica */
} dtc_info_t;

#ifdef __cplusplus
extern "C" {
#endif

void dtc_describe(uint16_t raw, dtc_info_t *out);

#ifdef __cplusplus
}
#endif

#endif
