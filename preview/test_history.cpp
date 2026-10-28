// Teste do historico da sessao no PC, sem hardware.
//
//   cd preview && make -f test_history.mk
//
// Duas coisas importam aqui. O anel: a ordem cronologica tem que continuar
// certa depois que o buffer da a volta, que e justamente quando um off-by-one
// passa despercebido no device (10 minutos de sessao para aparecer). E a
// compactacao: cada grandeza vira int16 em escala fixa, entao a ida e volta
// tem que preservar a precisao que a pagina mostra, saturar em vez de dar a
// volta, e manter "sem leitura" distinto de zero.

#include <cmath>
#include <cstdio>

#include "history.h"

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("  FALHOU: " __VA_ARGS__); printf("\n"); } } while (0)

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

static history_sample_t sample_water(float water)
{
    history_sample_t g = {};
    g.water_temp_c = water;
    return g;
}

int main(void)
{
    history_sample_t s;

    printf("== vazio ==\n");
    history_reset();
    CHECK(history_count() == 0, "historico zerado deve ter 0 amostras, veio %u", history_count());
    CHECK(!history_get(0, &s), "indice 0 em historico vazio nao existe");

    printf("== intervalo ==\n");
    history_reset();
    history_sample_t g = sample_water(90.0f);
    CHECK(history_tick(&g, 1000), "a primeira amostra entra na hora");
    CHECK(!history_tick(&g, 1000 + HISTORY_INTERVAL_MS - 1), "antes do intervalo nao entra");
    CHECK(history_tick(&g, 1000 + HISTORY_INTERVAL_MS), "no intervalo entra");
    CHECK(history_count() == 2, "deviam ser 2 amostras, vieram %u", history_count());

    printf("== ponteiro nulo nao entra ==\n");
    history_reset();
    CHECK(!history_tick(NULL, 2000), "ponteiro nulo nao deve explodir nem gravar");
    CHECK(history_count() == 0, "nada devia ter entrado, vieram %u", history_count());
    CHECK(!history_get(0, NULL), "destino nulo nao deve explodir");

    printf("== ordem cronologica antes de encher ==\n");
    history_reset();
    for (int i = 0; i < 5; i++) {
        history_sample_t x = sample_water((float)i);
        history_tick(&x, (uint32_t)(i * HISTORY_INTERVAL_MS));
    }
    CHECK(history_count() == 5, "deviam ser 5, vieram %u", history_count());
    for (int i = 0; i < 5; i++) {
        CHECK(history_get((uint16_t)i, &s) && s.water_temp_c == (float)i,
              "indice %d devia ser %d, veio %.1f", i, i, (double)s.water_temp_c);
    }

    printf("== o anel da a volta ==\n");
    history_reset();
    const int total = HISTORY_CAP + 7;        /* 7 a mais que a capacidade */
    for (int i = 0; i < total; i++) {
        history_sample_t x = sample_water((float)i);
        history_tick(&x, (uint32_t)(i * HISTORY_INTERVAL_MS));
    }
    CHECK(history_count() == HISTORY_CAP, "cheio deve parar em %d, veio %u",
          HISTORY_CAP, history_count());

    /* As 7 primeiras sairam: a mais antiga agora e a de valor 7. */
    CHECK(history_get(0, &s) && s.water_temp_c == 7.0f,
          "a mais antiga devia ser 7, veio %.1f", (double)s.water_temp_c);
    CHECK(history_get((uint16_t)(HISTORY_CAP - 1), &s) && s.water_temp_c == (float)(total - 1),
          "a mais recente devia ser %d, veio %.1f", total - 1, (double)s.water_temp_c);

    /* E o meio tem que estar em ordem, sem buraco nem repeticao. */
    bool ordered = true;
    for (int i = 0; i < HISTORY_CAP; i++) {
        if (!history_get((uint16_t)i, &s) || s.water_temp_c != (float)(i + 7)) { ordered = false; break; }
    }
    CHECK(ordered, "a ordem cronologica quebrou depois da volta do anel");
    CHECK(!history_get((uint16_t)HISTORY_CAP, &s), "indice alem do fim nao existe");

    printf("== rollover do relogio ==\n");
    history_reset();
    history_sample_t r = sample_water(42.0f);
    const uint32_t before_wrap = 0xFFFFFF00u;
    history_tick(&r, before_wrap);
    /* Mesmo instante + 1 s: ja passou do fim do uint32_t, mas ainda nao
     * fechou o intervalo, entao nao deve gravar. */
    CHECK(!history_tick(&r, (uint32_t)(before_wrap + 1000u)),
          "1 s depois, mesmo cruzando o rollover, ainda e cedo");
    /* Agora sim, o intervalo cheio por cima da virada. */
    CHECK(history_tick(&r, (uint32_t)(before_wrap + HISTORY_INTERVAL_MS)),
          "a subtracao em uint32_t deve absorver o rollover");
    CHECK(history_count() == 2, "deviam ser 2 amostras, vieram %u", history_count());

    printf("== compactacao: ida e volta com a precisao da pagina ==\n");
    history_reset();
    history_sample_t full = {};
    full.lambda            = 0.987f;
    full.water_temp_c      = 91.4f;
    full.battery_v         = 13.86f;
    full.boost_bar         = 1.234f;
    full.comb_pressure_bar = 211.7f;
    full.iat_c             = -12.3f;      /* negativo tem que sobreviver */
    full.oil_temp_c        = 104.6f;
    history_tick(&full, 0);
    CHECK(history_get(0, &s), "a amostra devia existir");
    CHECK(near(s.lambda, 0.987f, 0.0006f),             "lambda virou %.4f", (double)s.lambda);
    CHECK(near(s.water_temp_c, 91.4f, 0.06f),          "agua virou %.2f", (double)s.water_temp_c);
    CHECK(near(s.battery_v, 13.86f, 0.006f),           "bateria virou %.3f", (double)s.battery_v);
    CHECK(near(s.boost_bar, 1.234f, 0.0006f),          "boost virou %.4f", (double)s.boost_bar);
    CHECK(near(s.comb_pressure_bar, 211.7f, 0.06f),    "rampa virou %.2f", (double)s.comb_pressure_bar);
    CHECK(near(s.iat_c, -12.3f, 0.06f),                "admissao virou %.2f", (double)s.iat_c);
    CHECK(near(s.oil_temp_c, 104.6f, 0.06f),           "oleo virou %.2f", (double)s.oil_temp_c);

    printf("== sem leitura continua sem leitura, e satura em vez de dar a volta ==\n");
    history_reset();
    history_sample_t odd = {};
    odd.oil_temp_c        = NAN;          /* carro que nao publica 0x5C */
    odd.boost_bar         = 99.0f;        /* fora da faixa: 99000 nao cabe em int16 */
    odd.comb_pressure_bar = -99999.0f;
    odd.water_temp_c      = 0.0f;         /* zero e leitura, nao ausencia */
    history_tick(&odd, 0);
    CHECK(history_get(0, &s), "a amostra devia existir");
    CHECK(std::isnan(s.oil_temp_c), "NaN devia voltar NaN, veio %.2f", (double)s.oil_temp_c);
    CHECK(s.water_temp_c == 0.0f && !std::isnan(s.water_temp_c), "zero virou ausencia");
    CHECK(s.boost_bar > 32.0f, "acima da faixa devia saturar no topo, veio %.3f", (double)s.boost_bar);
    CHECK(s.comb_pressure_bar < -3000.0f, "abaixo da faixa devia saturar no fundo, veio %.1f",
          (double)s.comb_pressure_bar);

    printf(g_fail ? "\n%d verificacao(oes) falharam\n" : "\nok\n", g_fail);
    return g_fail ? 1 : 0;
}
