// Teste do perfil de veiculo no PC, sem hardware.
//
//   cd preview && make -f test_profile.mk
//
// Cobre as decisoes que mudam de carro para carro: quais PIDs entram no plano
// de polling a partir dos bitmaps de 0100/0120/0140, como elas se dividem nos
// pedidos de varios PIDs, e o boost com e sem barometro.

#include <cstdio>
#include <cstring>

#include "vehicle_profile.h"

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("  FALHOU: " __VA_ARGS__); printf("\n"); } } while (0)

/* Bitmap com os PIDs que o B48 responde no padrao: 05, 0B, 0F, 23, 33, 42, 5C.
 * Bit A7 do bloco 0x00 e o PID 0x01; D0 de cada bloco indica "ha mais adiante". */
static void set_pid(uint8_t support[][4], uint8_t pid)
{
    const uint8_t block  = (uint8_t)((pid - 1) / 0x20);
    const uint8_t offset = (uint8_t)((pid - 1) % 0x20);
    support[block][offset / 8] |= (uint8_t)(1u << (7 - (offset % 8)));
}

int main(void)
{
    const vehicle_profile_t *p = vehicle_profile();

    printf("== perfil ativo ==\n");
    CHECK(p != NULL, "perfil nulo");
    CHECK(p->protocol_cmd && strcmp(p->protocol_cmd, "ATSP6\r") == 0,
          "F20 LCI deve fixar ATSP6 (CAN 11 bit 500k), veio %s",
          p->protocol_cmd ? p->protocol_cmd : "(null)");
    CHECK(p->header_cmd && strcmp(p->header_cmd, "ATSH7E0\r") == 0,
          "F20 LCI deve enderecar so a DME (ATSH7E0)");
    CHECK(p->pids_per_request == 6, "a DME do F20 responde a 6 PIDs por pedido, veio %u",
          (unsigned)p->pids_per_request);

    printf("== vp_pid_supported ==\n");
    uint8_t sup[3][4] = {};
    set_pid(sup, 0x05);
    set_pid(sup, 0x0B);
    set_pid(sup, 0x0F);
    set_pid(sup, 0x23);
    set_pid(sup, 0x33);
    set_pid(sup, 0x42);
    CHECK(vp_pid_supported(sup, 3, 0x05), "0x05 deveria estar suportado");
    CHECK(vp_pid_supported(sup, 3, 0x23), "0x23 deveria estar suportado");
    CHECK(vp_pid_supported(sup, 3, 0x42), "0x42 deveria estar suportado");
    CHECK(!vp_pid_supported(sup, 3, 0x5C), "0x5C nao foi declarado");
    CHECK(!vp_pid_supported(sup, 1, 0x23), "sem o bloco 0x20 lido, 0x23 e desconhecido");
    CHECK(!vp_pid_supported(sup, 3, 0x00), "0x00 nao e PID de dado");
    CHECK(!vp_pid_supported(sup, 3, 0x61), "fora da faixa coberta pelos bitmaps");

    printf("== vp_build_plan ==\n");
    uint8_t plan[VP_METRIC_COUNT] = {};
    vp_metric_t plan_metrics[VP_METRIC_COUNT] = {};
    uint8_t n = vp_build_plan(p, sup, 3, plan, plan_metrics, VP_METRIC_COUNT);
    CHECK(n == 6, "esperava 6 PIDs (sem oleo, lambda e etanol), veio %u", (unsigned)n);
    CHECK(plan[0] == 0x05 && plan[1] == 0x42 && plan[2] == 0x0B &&
          plan[3] == 0x23 && plan[4] == 0x0F && plan[5] == 0x33,
          "ordem/conteudo do plano inesperados: %02X %02X %02X %02X %02X %02X",
          plan[0], plan[1], plan[2], plan[3], plan[4], plan[5]);
    for (uint8_t i = 0; i < n; i++) {
        CHECK(plan[i] != 0x5C, "oleo nao suportado nao pode entrar no plano");
    }

    printf("== fonte alternativa da rampa de combustivel ==\n");
    uint8_t sup59[3][4] = {};
    set_pid(sup59, 0x05);
    set_pid(sup59, 0x59);            /* so a absoluta */
    n = vp_build_plan(p, sup59, 3, plan, NULL, VP_METRIC_COUNT);
    bool has59 = false, has23 = false;
    for (uint8_t i = 0; i < n; i++) {
        if (plan[i] == 0x59) has59 = true;
        if (plan[i] == 0x23) has23 = true;
    }
    CHECK(has59 && !has23, "sem 0x23 o plano deve cair para 0x59");

    printf("== plano vazio ==\n");
    uint8_t empty[3][4] = {};
    CHECK(vp_build_plan(p, empty, 3, plan, NULL, VP_METRIC_COUNT) == 0,
          "bitmap zerado nao deve render PID");
    CHECK(vp_build_plan(p, sup, 0, plan, NULL, VP_METRIC_COUNT) == 0,
          "sem bitmap lido nao se adivinha PID");

    printf("== grandeza de cada posicao do plano ==\n");
    CHECK(plan_metrics[0] == VP_METRIC_WATER_TEMP && plan_metrics[2] == VP_METRIC_MAP &&
          plan_metrics[4] == VP_METRIC_IAT,
          "out_metrics nao acompanhou out_pids");

    printf("== lambda e etanol do F20 ==\n");
    uint8_t f20[3][4] = {};
    /* bitmaps lidos do carro pelo ENET (docs/pids-f20.md) */
    const uint8_t real[3][4] = { { 0xBE, 0x3F, 0xA8, 0x13 }, { 0xA0, 0x07, 0xB0, 0x11 },
                                 { 0xFE, 0xD0, 0xC4, 0x10 } };
    memcpy(f20, real, sizeof(f20));
    n = vp_build_plan(p, f20, 3, plan, plan_metrics, VP_METRIC_COUNT);
    CHECK(n == 9, "o F20 publica as 9 grandezas do perfil, veio %u", (unsigned)n);
    bool lambda34 = false, eth = false, cmd44 = false;
    for (uint8_t i = 0; i < n; i++) {
        if (plan[i] == 0x34) lambda34 = plan_metrics[i] == VP_METRIC_LAMBDA;
        if (plan[i] == 0x52) eth = plan_metrics[i] == VP_METRIC_ETHANOL;
        if (plan[i] == 0x44) cmd44 = true;
    }
    CHECK(lambda34, "lambda tem que vir da sonda (0x34)");
    CHECK(!cmd44, "lambda comandado (0x44) nao e leitura de sonda");
    CHECK(eth, "etanol (0x52) tem que entrar no plano");

    printf("== vp_metric_fast ==\n");
    /* O que o pe no acelerador move vai em todo pedido; o resto faz rodizio. */
    CHECK(vp_metric_fast(VP_METRIC_MAP), "boost precisa de todo pedido");
    CHECK(vp_metric_fast(VP_METRIC_FUEL_RAIL), "rampa de combustivel precisa de todo pedido");
    CHECK(vp_metric_fast(VP_METRIC_LAMBDA), "lambda precisa de todo pedido");
    CHECK(!vp_metric_fast(VP_METRIC_WATER_TEMP) && !vp_metric_fast(VP_METRIC_OIL_TEMP) &&
          !vp_metric_fast(VP_METRIC_ETHANOL) && !vp_metric_fast(VP_METRIC_BARO),
          "grandezas lentas nao podem ocupar todo pedido");

    printf("== vp_next_request: 6 por pedido ==\n");
    {
        const uint8_t fast[] = { 0x0B, 0x23, 0x34 };
        const uint8_t slow[] = { 0x05, 0x42, 0x0F, 0x5C, 0x52, 0x33 };
        vp_sched_t st = {};
        uint8_t out[6];
        int seen[256] = {};
        for (int r = 0; r < 2; r++) {
            uint8_t k = vp_next_request(fast, 3, slow, 6, 6, &st, out);
            CHECK(k == 6, "pedido %d deveria ir cheio, veio %u", r, (unsigned)k);
            CHECK(out[0] == 0x0B && out[1] == 0x23 && out[2] == 0x34,
                  "as rapidas abrem todo pedido");
            for (uint8_t i = 3; i < k; i++) seen[out[i]]++;
        }
        for (uint8_t i = 0; i < 6; i++)
            CHECK(seen[slow[i]] == 1, "em 2 pedidos cada lenta vem uma vez (0x%02X veio %d)",
                  slow[i], seen[slow[i]]);
        /* poucas lentas: nenhuma se repete no mesmo pedido */
        const uint8_t two[] = { 0x05, 0x42 };
        vp_sched_t st2 = {};
        uint8_t k = vp_next_request(fast, 3, two, 2, 6, &st2, out);
        CHECK(k == 5, "3 rapidas + 2 lentas, sem repetir, veio %u", (unsigned)k);
    }

    printf("== vp_next_request: um por pedido ==\n");
    {
        const uint8_t fast[] = { 0x0B, 0x23, 0x34 };
        const uint8_t slow[] = { 0x05, 0x42 };
        vp_sched_t st = {};
        uint8_t out[1], seq[8];
        for (int r = 0; r < 8; r++) {
            CHECK(vp_next_request(fast, 3, slow, 2, 1, &st, out) == 1, "um PID por pedido");
            seq[r] = out[0];
        }
        const uint8_t want[8] = { 0x0B, 0x23, 0x34, 0x05, 0x0B, 0x23, 0x34, 0x42 };
        CHECK(memcmp(seq, want, sizeof(want)) == 0,
              "volta = rapidas + uma lenta: %02X %02X %02X %02X %02X %02X %02X %02X",
              seq[0], seq[1], seq[2], seq[3], seq[4], seq[5], seq[6], seq[7]);
        vp_sched_t st3 = {};
        CHECK(vp_next_request(NULL, 0, NULL, 0, 6, &st3, out) == 0, "plano vazio nao gera pedido");
    }

    printf("== vp_next_request_packed: resposta num quadro CAN ==\n");
    {
        /* lambda primeiro: e o que o gateway faz com a lista de rapidas */
        const uint8_t fast[] = { 0x34, 0x0B, 0x23 };
        const uint8_t slow[] = { 0x05, 0x42, 0x0F, 0x5C, 0x52, 0x33 };
        vp_sched_t st = {};
        uint8_t out[6], k;
        int seen[256] = {};
        int eth_reqs = -1;
        for (int r = 0; r < 12; r++) {
            k = vp_next_request_packed(fast, 3, slow, 6, 6, 6, &st, out);
            int bytes = 0;
            for (uint8_t i = 0; i < k; i++) { bytes += 1 + vp_pid_data_len(out[i]); seen[out[i]]++; }
            CHECK(k >= 1 && bytes <= 6 || k == 1, "pedido %d estoura o quadro (%d bytes)", r, bytes);
            if (r == 0) CHECK(k == 1 && out[0] == 0x34, "a volta abre com a lambda sozinha (6 bytes)");
            if (r == 1) CHECK(k == 2 && out[0] == 0x0B && out[1] == 0x23, "turbo + rampa no mesmo quadro");
            for (uint8_t i = 0; i < k; i++) if (out[i] == 0x52 && eth_reqs < 0) eth_reqs = r;
        }
        CHECK(seen[0x34] == 4 && seen[0x0B] == 4, "em 12 pedidos, lambda e turbo vem 4 vezes (1 a cada 3): %d %d",
              seen[0x34], seen[0x0B]);
        for (uint8_t i = 0; i < 6; i++)
            CHECK(seen[slow[i]] >= 1, "lenta 0x%02X nunca foi pedida em 12 pedidos", slow[i]);
        CHECK(eth_reqs >= 0 && eth_reqs < 9, "etanol tem que vir nas primeiras 3 voltas (veio no pedido %d)", eth_reqs);
        vp_sched_t st2 = {};
        CHECK(vp_next_request_packed(NULL, 0, NULL, 0, 6, 6, &st2, out) == 0, "plano vazio nao gera pedido");
        const uint8_t only_fast[] = { 0x0B };
        vp_sched_t st3 = {};
        CHECK(vp_next_request_packed(only_fast, 1, NULL, 0, 6, 6, &st3, out) == 1 &&
              vp_next_request_packed(only_fast, 1, NULL, 0, 6, 6, &st3, out) == 1, "sem lentas a volta recomeca");
    }

    printf("== vp_pid_data_len ==\n");
    CHECK(vp_pid_data_len(0x0B) == 1 && vp_pid_data_len(0x23) == 2 && vp_pid_data_len(0x34) == 4 &&
          vp_pid_data_len(0x52) == 1 && vp_pid_data_len(0x42) == 2 && vp_pid_data_len(0x5C) == 1,
          "tamanho de PID errado parte a resposta multi-PID no lugar errado");
    CHECK(vp_pid_data_len(0x00) == 0, "PID desconhecido deve parar o parser");

    printf("== vp_boost_bar ==\n");
    /* 200 kPa no coletor com 101 kPa de barometro ~ 0.99 bar de sobrepressao */
    CHECK(vp_boost_bar(200, 101.0f) > 0.98f && vp_boost_bar(200, 101.0f) < 1.00f,
          "boost com barometro saiu %.3f", vp_boost_bar(200, 101.0f));
    /* sem barometro usa nivel do mar */
    CHECK(vp_boost_bar(200, 0.0f) > 0.98f && vp_boost_bar(200, 0.0f) < 0.99f,
          "boost sem barometro saiu %.3f", vp_boost_bar(200, 0.0f));
    /* em altitude (Sao Paulo ~ 93 kPa) o mesmo coletor rende mais sobrepressao */
    CHECK(vp_boost_bar(200, 93.0f) > vp_boost_bar(200, 101.325f),
          "barometro menor deveria dar boost maior");
    /* motor parado/aspirado: pressao negativa, nao zero */
    CHECK(vp_boost_bar(40, 101.0f) < -0.5f, "vacuo deveria sair negativo");

    printf(g_fail == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d FALHA(S)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
