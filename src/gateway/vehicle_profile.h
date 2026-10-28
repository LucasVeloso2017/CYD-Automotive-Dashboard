#ifndef VEHICLE_PROFILE_H
#define VEHICLE_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Perfil de veiculo: o que muda de carro para carro no acesso ao OBD2.
 *
 * O dashboard nasceu generico (ATSP0 + lista fixa de PIDs). Em carro que nao
 * publica algum PID, isso custava uma pergunta por rodada eternamente, e o
 * oleo chegava a ser estimado a partir da agua — numero inventado na tela.
 *
 * O perfil resolve as duas coisas: fixa o protocolo quando ele e conhecido
 * (evita o ATSP0 ficar procurando) e declara, por grandeza, as fontes
 * aceitaveis em ordem de preferencia. Quais delas serao usadas e decidido em
 * tempo de sessao pelos bitmaps de PID suportado (0100/0120/0140).
 */

/** Grandeza exibida no painel. */
typedef enum {
    VP_METRIC_WATER_TEMP = 0,
    VP_METRIC_BATTERY,
    VP_METRIC_MAP,          /* vira boost depois de descontar o barometro */
    VP_METRIC_FUEL_RAIL,
    VP_METRIC_IAT,
    VP_METRIC_OIL_TEMP,
    VP_METRIC_LAMBDA,       /* sonda medida (0x34), nunca o lambda comandado */
    VP_METRIC_ETHANOL,      /* % de etanol no combustivel (0x52) */
    VP_METRIC_BARO,         /* sem gauge: entra no calculo do boost */
    VP_METRIC_COUNT
} vp_metric_t;

#define VP_MAX_SOURCES 3

typedef struct {
    vp_metric_t metric;
    uint8_t     pids[VP_MAX_SOURCES];   /* 0x00 encerra a lista */
} vp_metric_sources_t;

typedef struct {
    const char *name;
    /** Comando de protocolo do ELM327 (ex.: "ATSP6\r"); NULL usa ATSP0. */
    const char *protocol_cmd;
    /** Cabecalho fisico (ex.: "ATSH7E0\r" = so a DME); NULL fala em broadcast.
     *  Com uma ECU so respondendo, o pedido pode levar a contagem de respostas
     *  e o ELM327 devolve assim que ela chega, sem esperar o timeout. */
    const char *header_cmd;
    /** Quantos PIDs cabem num pedido de modo 01 (1..6). 1 = um por pedido. */
    uint8_t pids_per_request;
    const vp_metric_sources_t *metrics;
    uint8_t metric_count;
} vehicle_profile_t;

/** Perfil ativo da build (ver VEHICLE_PROFILE_BMW_F20_LCI). */
const vehicle_profile_t *vehicle_profile(void);

/**
 * O PID aparece como suportado nos bitmaps de 0100/0120/0140?
 *
 * @param support  Bitmaps na ordem 0x00, 0x20, 0x40, cada um 4 bytes (A..D).
 * @param have     Quantos bitmaps foram lidos de fato (0..3).
 * @param pid      PID consultado (0x01..0x60).
 * @return `true` quando o bit do PID esta ligado; `false` tambem quando o
 *         bitmap que o cobre nao foi lido — na duvida nao se consulta, para
 *         nao gastar rodada com PID que o carro nao responde.
 */
bool vp_pid_supported(const uint8_t support[][4], uint8_t have, uint8_t pid);

/**
 * A grandeza vai em todo pedido?
 *
 * Boost, rampa de combustivel e lambda seguem o pe no acelerador e entram em
 * todo pedido. As demais (agua, oleo, admissao, tensao, etanol, barometro) se
 * movem em segundos ou minutos e dividem, em rodizio, os slots que sobram.
 */
bool vp_metric_fast(vp_metric_t metric);

/** Rodizio do agendador de pedidos (ver vp_next_request). Zere para comecar. */
typedef struct {
    uint8_t pos;        /* posicao na volta, no modo sem espaco para as lentas */
    uint8_t slow_pos;   /* proxima grandeza lenta da vez */
} vp_sched_t;

/**
 * Monta o proximo pedido de modo 01.
 *
 * Com espaco para todas as rapidas e mais uma (`max > n_fast`), cada pedido
 * leva todas as rapidas e completa os slots com as lentas em rodizio. Sem
 * espaco (ELM327 que so aceita um PID por vez), anda por uma volta de
 * "todas as rapidas + uma lenta", `max` itens por pedido.
 *
 * @param fast    PIDs rapidos.     @param n_fast quantos.
 * @param slow    PIDs lentos.      @param n_slow quantos.
 * @param max     PIDs por pedido (>= 1).
 * @param st      Estado do rodizio, preservado entre chamadas.
 * @param out     Saida, com espaco para `max` PIDs.
 * @return Quantos PIDs vao no pedido (0 so com as duas listas vazias).
 */
uint8_t vp_next_request(const uint8_t *fast, uint8_t n_fast,
                        const uint8_t *slow, uint8_t n_slow,
                        uint8_t max, vp_sched_t *st, uint8_t *out);

/**
 * Monta o proximo pedido cuja resposta cabe num quadro CAN so.
 *
 * Para adaptadores que nao aguentam resposta multi-frame (muitos clones cortam
 * no primeiro quadro), mas aceitam varios PIDs por pedido. A resposta inteira
 * ("41" + PID e dados de cada um) tem que caber em `budget` bytes depois do
 * 0x41 — 6 num single frame CAN.
 *
 * Volta: as rapidas em pedidos que enchem o orcamento, na ordem da lista, e
 * depois um pedido de lentas em rodizio. Uma rapida que sozinha estoura o
 * orcamento vai sozinha (o adaptador entrega o que couber).
 *
 * @param budget  Bytes de resposta por pedido, sem contar o 0x41 (6 em CAN).
 * @param max     Teto de PIDs por pedido.
 * @return Quantos PIDs vao no pedido (0 so com as duas listas vazias).
 */
uint8_t vp_next_request_packed(const uint8_t *fast, uint8_t n_fast,
                               const uint8_t *slow, uint8_t n_slow,
                               uint8_t budget, uint8_t max, vp_sched_t *st, uint8_t *out);

/**
 * Bytes de dado que o PID devolve no modo 01 (sem contar o proprio PID).
 *
 * Numa resposta com varios PIDs nao ha separador: e o tamanho de cada um que
 * diz onde comeca o proximo.
 *
 * @return 0 para PID cujo tamanho nao e conhecido aqui — o parser para nele.
 */
uint8_t vp_pid_data_len(uint8_t pid);

/**
 * Escolhe, para cada grandeza do perfil, a primeira fonte suportada.
 *
 * @param profile     Perfil ativo.
 * @param support     Bitmaps lidos.
 * @param have        Quantos bitmaps foram lidos.
 * @param out_pids    Saida: PIDs a consultar, na ordem das grandezas.
 * @param out_metrics Saida opcional (pode ser NULL): a grandeza de cada PID,
 *                    para separar as rapidas das lentas.
 * @param cap         Capacidade de `out_pids` (e de `out_metrics`).
 * @return Quantos PIDs entraram no plano.
 */
uint8_t vp_build_plan(const vehicle_profile_t *profile,
                      const uint8_t support[][4],
                      uint8_t have,
                      uint8_t *out_pids,
                      vp_metric_t *out_metrics,
                      uint8_t cap);

/**
 * Pressao de sobrealimentacao em bar relativos.
 *
 * @param map_kpa   PID 0x0B (pressao absoluta no coletor, kPa).
 * @param baro_kpa  PID 0x33, ou <= 0 quando o carro nao publica.
 * @return bar relativos; sem barometro usa 101.325 kPa (nivel do mar), que e
 *         o que o firmware fazia antes — em altitude o valor sai otimista.
 */
float vp_boost_bar(uint8_t map_kpa, float baro_kpa);

#ifdef __cplusplus
}
#endif

#endif
