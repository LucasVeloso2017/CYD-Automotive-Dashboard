#ifndef DTC_DATA_H
#define DTC_DATA_H

#include <stdbool.h>
#include <stdint.h>

/* Snapshot dos codigos de falha (DTC) lido do ELM327. A task de polling
 * escreve; a tela DTC le por elm327_dtc_copy(), sob secao critica.
 *
 * Codigos ficam em bruto (2 bytes, formato SAE J2012); dtc_format() vira
 * "P0301". ~100 bytes no total. */
#define DTC_MAX_CODES   12

typedef struct {
    bool     valid;        /* houve pelo menos uma leitura completa */
    uint32_t read_ms;      /* lv_tick/millis da leitura */
    bool     mil;          /* luz da injecao acesa (PID 01 bit 7) */
    uint8_t  ecu_count;    /* quantos codigos a ECU diz ter (PID 01 & 0x7F) */

    uint8_t  n_stored, n_pending, n_permanent;
    uint16_t stored[DTC_MAX_CODES];      /* modo 03: confirmados */
    uint16_t pending[DTC_MAX_CODES];     /* modo 07: vistos uma vez */
    uint16_t permanent[DTC_MAX_CODES];   /* modo 0A: so o carro apaga */

    /* Freeze frame 0 (modo 02): o instante em que o primeiro DTC gravou. */
    struct {
        bool     valid;
        uint16_t dtc;
        uint16_t rpm;
        uint8_t  speed_kmh;
        int16_t  coolant_c;
        uint8_t  load_pct;
        uint8_t  throttle_pct;
        uint8_t  map_kpa;
    } ff;
} dtc_snapshot_t;

/* O que a task esta fazendo agora; a UI mostra spinner fora de IDLE. */
typedef enum {
    DTC_OP_IDLE = 0,
    DTC_OP_READING,
    DTC_OP_CLEARING,
} dtc_op_t;

/* Resultado do ultimo modo 04. */
typedef enum {
    DTC_CLEAR_NONE = 0,       /* nenhum apagamento nesta sessao */
    DTC_CLEAR_OK,             /* alguma ECU respondeu 44 */
    DTC_CLEAR_REFUSED,        /* 7F 04 xx: a ECU recusou (ver nrc; 0x22 = condicoes, tipico com motor ligado) */
    DTC_CLEAR_NO_RESPONSE,    /* NO DATA, erro ou timeout */
} dtc_clear_result_t;

#ifdef __cplusplus
extern "C" {
#endif

/* "P0301" a partir do codigo bruto. out precisa de 6 bytes. */
void dtc_format(uint16_t raw, char *out);

#ifdef __cplusplus
}
#endif

#endif
