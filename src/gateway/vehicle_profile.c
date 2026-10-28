#include "vehicle_profile.h"

#include <stddef.h>

/* ---- Perfis -------------------------------------------------------------
 *
 * BMW 120i F20 LCI (facelift 2015-2019, motor B48B20):
 *
 * - Protocolo: ISO 15765-4 CAN 11 bit / 500 kbps na tomada OBD (pinos 6 e 14),
 *   que no ELM327 e o ATSP6. Fixar evita o ATSP0 varrer protocolos a cada
 *   conexao — em F-series a varredura chega a custar alguns segundos.
 * - Agua (0x05), tensao do modulo (0x42), coletor (0x0B) e admissao (0x0F) sao
 *   PIDs padrao e o DME responde.
 * - Combustivel: injecao direta publica a rampa de alta pressao em 0x23
 *   (manometrica) ou 0x59 (absoluta); as duas tem fator 10 kPa, entao a
 *   conversao e a mesma e basta escolher a que o carro aceitar.
 * - Oleo: 0x5C e opcional. Quando o DME nao publica, o dado existe na UDS da
 *   BMW (header 6F1, alvo 12), que exige descobrir o DID no proprio carro —
 *   nao ha palpite aqui: sem 0x5C o gauge fica indisponivel em vez de mostrar
 *   numero estimado.
 * - Lambda: 0x34 e a sonda de banda larga medida. 0x44 (lambda comandado) nao
 *   entra: e o alvo da DME, e no painel pareceria leitura da sonda.
 * - Etanol: 0x52, publicado pela DME (lido 29 % com gasolina E30).
 *
 * Tudo isto foi conferido no carro pelo ENET (docs/pids-f20.md), inclusive
 * que a DME responde a ate 6 PIDs num pedido so, no mesmo tempo de um. Com o
 * ATSH7E0 so ela responde, e o pedido inteiro custa uma troca na serial.
 */
static const vp_metric_sources_t BMW_F20_LCI_METRICS[] = {
    { VP_METRIC_WATER_TEMP, { 0x05, 0x00, 0x00 } },
    { VP_METRIC_BATTERY,    { 0x42, 0x00, 0x00 } },
    { VP_METRIC_MAP,        { 0x0B, 0x00, 0x00 } },
    { VP_METRIC_FUEL_RAIL,  { 0x23, 0x59, 0x00 } },
    { VP_METRIC_IAT,        { 0x0F, 0x00, 0x00 } },
    { VP_METRIC_OIL_TEMP,   { 0x5C, 0x00, 0x00 } },
    { VP_METRIC_LAMBDA,     { 0x34, 0x24, 0x00 } },
    { VP_METRIC_ETHANOL,    { 0x52, 0x00, 0x00 } },
    { VP_METRIC_BARO,       { 0x33, 0x00, 0x00 } },
};

static const vehicle_profile_t BMW_F20_LCI = {
    "BMW 120i F20 LCI (B48)",
    "ATSP6\r",
    "ATSH7E0\r",
    6,
    BMW_F20_LCI_METRICS,
    (uint8_t)(sizeof(BMW_F20_LCI_METRICS) / sizeof(BMW_F20_LCI_METRICS[0])),
};

/* Generico, para qualquer carro OBD2: broadcast e um PID por pedido. Sem
 * saber quantas ECUs respondem, nao da para pedir a contagem de respostas, e
 * nem todo carro aceita varios PIDs juntos. */
static const vp_metric_sources_t GENERIC_METRICS[] = {
    { VP_METRIC_WATER_TEMP, { 0x05, 0x00, 0x00 } },
    { VP_METRIC_BATTERY,    { 0x42, 0x00, 0x00 } },
    { VP_METRIC_MAP,        { 0x0B, 0x00, 0x00 } },
    { VP_METRIC_FUEL_RAIL,  { 0x23, 0x59, 0x00 } },
    { VP_METRIC_IAT,        { 0x0F, 0x00, 0x00 } },
    { VP_METRIC_OIL_TEMP,   { 0x5C, 0x00, 0x00 } },
    { VP_METRIC_LAMBDA,     { 0x34, 0x24, 0x00 } },
    { VP_METRIC_ETHANOL,    { 0x52, 0x00, 0x00 } },
    { VP_METRIC_BARO,       { 0x33, 0x00, 0x00 } },
};

static const vehicle_profile_t GENERIC = {
    "Generico (OBD2)",
    NULL,                 /* ATSP0: deixa o ELM327 descobrir */
    NULL,
    1,
    GENERIC_METRICS,
    (uint8_t)(sizeof(GENERIC_METRICS) / sizeof(GENERIC_METRICS[0])),
};

/* Tabela em vez de #ifdef solto: as duas entradas ficam referenciadas, e o
 * compilador nao reclama do perfil que nao foi escolhido nesta build. */
static const vehicle_profile_t *const PROFILES[] = { &BMW_F20_LCI, &GENERIC };

const vehicle_profile_t *vehicle_profile(void)
{
#ifdef VEHICLE_PROFILE_GENERIC
    return PROFILES[1];
#else
    return PROFILES[0];
#endif
}

/* ---- Suporte de PID ---------------------------------------------------- */

bool vp_pid_supported(const uint8_t support[][4], uint8_t have, uint8_t pid)
{
    if (!support || pid == 0x00 || pid > 0x60) return false;

    const uint8_t block = (uint8_t)((pid - 1) / 0x20);   /* 0x01..0x20 -> 0 */
    if (block >= have) return false;

    const uint8_t offset = (uint8_t)((pid - 1) % 0x20);  /* 0..31 */
    const uint8_t byte   = (uint8_t)(offset / 8);        /* A..D */
    const uint8_t bit    = (uint8_t)(7 - (offset % 8));  /* A7 = PID mais baixo */

    return (support[block][byte] & (uint8_t)(1u << bit)) != 0;
}

bool vp_metric_fast(vp_metric_t metric)
{
    return metric == VP_METRIC_MAP || metric == VP_METRIC_FUEL_RAIL || metric == VP_METRIC_LAMBDA;
}

uint8_t vp_next_request(const uint8_t *fast, uint8_t n_fast,
                        const uint8_t *slow, uint8_t n_slow,
                        uint8_t max, vp_sched_t *st, uint8_t *out)
{
    if (!st || !out || max == 0) return 0;
    if (n_fast == 0 && n_slow == 0) return 0;
    if (st->slow_pos >= n_slow) st->slow_pos = 0;

    uint8_t n = 0;
    if (max > n_fast || n_slow == 0) {
        /* Cabe tudo que e rapido e sobra lugar: as lentas completam o pedido,
         * cada uma no maximo uma vez. */
        for (uint8_t i = 0; i < n_fast && n < max; i++) out[n++] = fast[i];
        for (uint8_t k = 0; k < n_slow && n < max; k++) {
            out[n++] = slow[st->slow_pos];
            st->slow_pos = (uint8_t)((st->slow_pos + 1) % n_slow);
        }
        return n;
    }

    /* Volta de "rapidas + uma lenta", fatiada em pedidos de `max`. */
    const uint8_t round_len = (uint8_t)(n_fast + 1);
    if (st->pos >= round_len) st->pos = 0;
    while (n < max) {
        if (st->pos < n_fast) {
            out[n++] = fast[st->pos];
        } else {
            out[n++] = slow[st->slow_pos];
            st->slow_pos = (uint8_t)((st->slow_pos + 1) % n_slow);
        }
        st->pos = (uint8_t)((st->pos + 1) % round_len);
        if (st->pos == 0) break;          /* volta fechada: o proximo pedido recomeca */
    }
    return n;
}

uint8_t vp_next_request_packed(const uint8_t *fast, uint8_t n_fast,
                               const uint8_t *slow, uint8_t n_slow,
                               uint8_t budget, uint8_t max, vp_sched_t *st, uint8_t *out)
{
    if (!st || !out || max == 0) return 0;
    if (n_fast == 0 && n_slow == 0) return 0;
    if (st->slow_pos >= n_slow) st->slow_pos = 0;
    if (st->pos > n_fast || (st->pos == n_fast && n_slow == 0)) st->pos = 0;

    uint8_t n = 0, used = 0;

    if (st->pos < n_fast) {
        /* rapidas, em ordem, ate encher o orcamento deste pedido */
        while (st->pos < n_fast && n < max) {
            const uint8_t cost = (uint8_t)(1 + vp_pid_data_len(fast[st->pos]));
            if (n > 0 && used + cost > budget) break;
            out[n++] = fast[st->pos++];
            used = (uint8_t)(used + cost);
        }
        if (st->pos >= n_fast && n_slow == 0) st->pos = 0;
        return n;
    }

    /* fim da volta: um pedido de lentas, em rodizio */
    const uint8_t start = st->slow_pos;
    while (n < max) {
        const uint8_t cost = (uint8_t)(1 + vp_pid_data_len(slow[st->slow_pos]));
        if (n > 0 && used + cost > budget) break;
        out[n++] = slow[st->slow_pos];
        used = (uint8_t)(used + cost);
        st->slow_pos = (uint8_t)((st->slow_pos + 1) % n_slow);
        if (st->slow_pos == start) break;          /* deu a volta nas lentas */
    }
    st->pos = 0;
    return n;
}

uint8_t vp_pid_data_len(uint8_t pid)
{
    switch (pid) {
    case 0x05: case 0x0B: case 0x0F: case 0x33:
    case 0x51: case 0x52: case 0x5C:
        return 1;
    case 0x15: case 0x23: case 0x42: case 0x44: case 0x59:
        return 2;
    case 0x24: case 0x34:
        return 4;
    default:
        return 0;
    }
}

uint8_t vp_build_plan(const vehicle_profile_t *profile,
                      const uint8_t support[][4],
                      uint8_t have,
                      uint8_t *out_pids,
                      vp_metric_t *out_metrics,
                      uint8_t cap)
{
    if (!profile || !out_pids || cap == 0) return 0;

    uint8_t n = 0;
    for (uint8_t m = 0; m < profile->metric_count && n < cap; m++) {
        const uint8_t *pids = profile->metrics[m].pids;
        for (uint8_t s = 0; s < VP_MAX_SOURCES && pids[s] != 0x00; s++) {
            if (!vp_pid_supported(support, have, pids[s])) continue;
            if (out_metrics) out_metrics[n] = profile->metrics[m].metric;
            out_pids[n++] = pids[s];
            break;                 /* uma fonte por grandeza */
        }
    }
    return n;
}

float vp_boost_bar(uint8_t map_kpa, float baro_kpa)
{
    const float baro = (baro_kpa > 0.0f) ? baro_kpa : 101.325f;
    return ((float)map_kpa - baro) / 100.0f;
}
