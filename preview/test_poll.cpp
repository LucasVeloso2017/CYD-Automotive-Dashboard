// Teste do polling multi-PID do elm327_gateway no PC, sem hardware.
//
//   cd preview && make -f test_poll.mk
//
// As respostas seguem o formato do ELM327 com ATE0/ATL0/ATS0/ATH0. Os bytes
// sao os que a DME do F20 devolveu pelo ENET para o mesmo pedido
// (docs/pids-f20.md); so o enquadramento (linha de tamanho e "N:") e do ELM.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bt_gateway.h"
#include "elm327_gateway.h"
#include "vehicle_data.h"

uint8_t elm327_test_parse(const char *resp, bool *truncated);   /* linkage C++, como no .cpp */
void    elm327_test_set_plan(const uint8_t *fast, uint8_t n_fast,
                             const uint8_t *slow, uint8_t n_slow, int mode);
bool    elm327_test_poll(void);
bool    elm327_test_init(void);
bool    elm327_test_physical(void);
int     elm327_test_count(void);
uint8_t elm327_test_parse_msgs(const char *resp);
int     elm327_test_mode(void);

enum { MULTI_COUNT = 0, MULTI = 1, PACKED = 2, SINGLE = 3 };

/* ---- stubs de Arduino / FreeRTOS / BT --------------------------------- */
SerialStub Serial;
static unsigned long g_ms = 100000;
unsigned long millis(void) { return g_ms += 7; }
void delay(unsigned long) {}
void vTaskDelay(uint32_t) {}
int xTaskCreate(void (*)(void *), const char *, uint32_t, void *, int, TaskHandle_t *) { return pdPASS; }
void vTaskDelete(TaskHandle_t) {}
extern "C" {
bt_device_state_t bt_state = {};
bool bt_is_connected(void) { return true; }
}

static std::map<std::string, std::string> g_script;   /* comando -> resposta */
/* Respostas quando o cabecalho e 7E0 (vazio = o gateway do carro ignora o
 * pedido direto, que e o bug que deixou o painel em zero). */
static std::map<std::string, std::string> g_script_7e0;
static std::string g_header = "7DF";
static std::string g_last_cmd;
static std::string g_all_cmds;                         /* todos os comandos, em ordem */
static std::string g_rx;
static size_t g_rx_pos = 0;

extern "C" {
int bt_serial_available(void) { return (int)(g_rx.size() - g_rx_pos); }
int bt_serial_read(void) { return g_rx_pos < g_rx.size() ? (unsigned char)g_rx[g_rx_pos++] : -1; }
void bt_serial_print(const char *cmd) {
    g_last_cmd = cmd;
    g_all_cmds += cmd;
    std::string c = cmd;
    if (c.rfind("ATSH", 0) == 0) { g_header = c.substr(4, 3); g_rx = "OK\r\r>"; g_rx_pos = 0; return; }
    if (c.rfind("AT", 0) == 0) { g_rx = (c == "ATDPN\r") ? "A6\r\r>" : "OK\r\r>"; g_rx_pos = 0; return; }
    const auto &m = (g_header == "7E0") ? g_script_7e0 : g_script;
    auto it = m.find(cmd);
    g_rx = (it != m.end()) ? it->second : "NO DATA\r\r>";
    g_rx_pos = 0;
}
}

/* ---- harness ----------------------------------------------------------- */
static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("  FALHOU: " __VA_ARGS__); printf("\n"); } } while (0)
static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

/* 0B 23 34 0F 42 05 do F20: 18 bytes, tres quadros, o ultimo com enchimento. */
static const char *F20_MULTI =
    "012\r0:410B64230488\r1:3485A080070F47\r2:42327805720000\r\r>";

int main(void)
{
    const uint8_t fast[] = { 0x0B, 0x23, 0x34 };
    const uint8_t slow[] = { 0x0F, 0x42, 0x05 };
    bool trunc = false;

    printf("== multi-frame com varios PIDs ==\n");
    memset(&vehicle.gauges, 0, sizeof(vehicle.gauges));
    CHECK(elm327_test_parse(F20_MULTI, &trunc) == 6, "esperava 6 PIDs");
    CHECK(!trunc, "mensagem completa nao e truncada");
    const vehicle_gauge_data_t &g = vehicle.gauges;
    CHECK(near(g.comb_pressure_bar, 116.0f, 0.01f), "rampa %.2f, esperava 116.0", g.comb_pressure_bar);
    CHECK(near(g.lambda, 1.0440f, 0.001f), "lambda %.4f, esperava 1.0440", g.lambda);
    CHECK(near(g.iat_c, 31.0f, 0.01f), "admissao %.1f, esperava 31", g.iat_c);
    CHECK(near(g.battery_v, 12.92f, 0.001f), "bateria %.3f, esperava 12.920", g.battery_v);
    CHECK(near(g.water_temp_c, 74.0f, 0.01f), "agua %.1f, esperava 74 (enchimento lido como PID?)", g.water_temp_c);

    printf("== etanol, oleo e barometro ==\n");
    CHECK(elm327_test_parse("00A\r0:415C63524B33\r1:6400000000000000\r\r>", &trunc) == 3,
          "esperava 3 PIDs");
    CHECK(near(g.oil_temp_c, 59.0f, 0.01f), "oleo %.1f, esperava 59", g.oil_temp_c);
    CHECK(near(g.ethanol_pct, 29.41f, 0.01f), "etanol %.2f, esperava 29.41", g.ethanol_pct);
    /* barometro de 100 kPa vale para o proximo 0x0B */
    CHECK(elm327_test_parse("410B64\r\r>", &trunc) == 1, "MAP sozinho");
    CHECK(near(g.boost_bar, 0.0f, 0.001f), "100 kPa com baro 100 = 0 bar, veio %.3f", g.boost_bar);

    printf("== single frame e varias ECUs ==\n");
    CHECK(elm327_test_parse("4105610F44\r\r>", &trunc) == 2, "single frame com 2 PIDs");
    CHECK(near(g.water_temp_c, 57.0f, 0.01f) && near(g.iat_c, 28.0f, 0.01f), "valores do single frame");
    CHECK(elm327_test_parse("SEARCHING...\r41055A\r410560\r\r>", &trunc) == 2,
          "uma mensagem por ECU no broadcast");

    printf("== lambda fora de faixa ==\n");
    vehicle.gauges.lambda = 1.0f;
    elm327_test_parse("4134FFFF8000\r\r>", &trunc);
    CHECK(near(g.lambda, 1.0f, 0.0001f), "0xFFFF (corte) nao pode virar lambda, veio %.3f", g.lambda);

    printf("== truncado ==\n");
    CHECK(elm327_test_parse("012\r0:410B64230488\r\r>", &trunc) == 2, "o que chegou ainda vale");
    CHECK(trunc, "faltou quadro: tem que acusar truncado");
    CHECK(elm327_test_parse("NO DATA\r\r>", &trunc) == 0, "NO DATA nao rende PID");

    printf("== pedido montado ==\n");
    elm327_test_set_plan(fast, 3, slow, 3, MULTI_COUNT);
    g_script = { { "010B23340F42051\r", F20_MULTI } };
    const uint32_t rev = vehicle.gauges.data_rev;
    CHECK(elm327_test_poll(), "pedido com resposta valida");
    CHECK(g_last_cmd == "010B23340F42051\r", "comando: %s", g_last_cmd.c_str());
    CHECK(vehicle.gauges.data_rev == rev + 1, "uma notificacao por resposta, nao por PID");

    printf("== escada de modos ==\n");
    elm327_test_set_plan(fast, 3, slow, 3, MULTI_COUNT);
    g_script = { { "010B23340F42051\r", "012\r0:410B64230488\r\r>" } };   /* contagem corta o multi-frame */
    for (int i = 0; i < 3; i++) elm327_test_poll();
    CHECK(elm327_test_mode() == MULTI, "3 truncados seguidos: tira a contagem (modo %d)", elm327_test_mode());
    g_script = { { "010B23340F4205\r", F20_MULTI } };
    CHECK(elm327_test_poll() && g_last_cmd == "010B23340F4205\r", "sem o digito de contagem: %s",
          g_last_cmd.c_str());
    CHECK(elm327_test_mode() == MULTI, "resposta boa nao desce de modo");

    g_script.clear();                                                       /* tudo NO DATA */
    g_all_cmds.clear();
    for (int i = 0; i < 3; i++) elm327_test_poll();
    CHECK(g_all_cmds.find("ATSH7DF\r") != std::string::npos,
          "DME muda em 7E0: antes de desistir do multi, tenta o broadcast");
    CHECK(elm327_test_mode() == MULTI, "em broadcast ainda tenta varios PIDs (modo %d)", elm327_test_mode());
    for (int i = 0; i < 3; i++) elm327_test_poll();
    CHECK(elm327_test_mode() == PACKED, "multi sem resposta em broadcast: agrupado num quadro (modo %d)",
          elm327_test_mode());
    for (int i = 0; i < 3; i++) elm327_test_poll();
    CHECK(elm327_test_mode() == SINGLE, "nem agrupado: um PID por vez (modo %d)", elm327_test_mode());
    g_script = { { "010B\r", "410B64\r\r>" } };
    CHECK(elm327_test_poll() && g_last_cmd == "010B\r", "um PID por pedido: %s", g_last_cmd.c_str());
    for (int i = 0; i < 5; i++) elm327_test_poll();
    CHECK(elm327_test_mode() == SINGLE, "ultimo degrau nao tem para onde descer");

    printf("== agrupado: cada resposta num quadro CAN ==\n");
    {
        const uint8_t fast_l[] = { 0x34, 0x0B, 0x23 };
        elm327_test_set_plan(fast_l, 3, slow, 3, PACKED);
        g_script = {
            { "0134\r",     "4134859880 07\r\r>" },
            { "010B23\r",   "410B64230488\r\r>" },
            { "010F42\r",   "410F4742327 8\r\r>" },
        };
        vehicle.gauges.lambda = 0.0f;
        CHECK(elm327_test_poll() && g_last_cmd == "0134\r", "lambda sozinha: %s", g_last_cmd.c_str());
        CHECK(near(vehicle.gauges.lambda, 1.0440f, 0.001f), "lambda do pedido agrupado: %.4f", vehicle.gauges.lambda);
        CHECK(elm327_test_poll() && g_last_cmd == "010B23\r", "turbo + rampa: %s", g_last_cmd.c_str());
        CHECK(elm327_test_poll() && g_last_cmd == "010F42\r", "lentas que cabem num quadro: %s", g_last_cmd.c_str());
        CHECK(elm327_test_mode() == PACKED, "respostas boas nao descem de modo");
    }

    printf("== lambda no primeiro quadro ==\n");
    {
        /* adaptador que corta o multi-frame no primeiro quadro: com a lambda na
         * frente do pedido, ela chega mesmo assim */
        vehicle.gauges.lambda = 0.0f;
        CHECK(elm327_test_parse("012\r0:413485A08007\r\r>", &trunc) == 1 && trunc, "so o primeiro quadro");
        CHECK(near(vehicle.gauges.lambda, 1.0440f, 0.001f), "lambda do primeiro quadro: %.4f", vehicle.gauges.lambda);
    }

    printf("== init: gateway ignora 7E0 ==\n");
    {
        /* Duas ECUs em broadcast (DME e cambio), cada uma com seu bitmap. */
        g_script = {
            { "0100\r", "4100BE3FA813\r410080000001\r\r>" },
            { "0120\r", "4120A007B011\r\r>" },
            { "0140\r", "4140FED0C410\r\r>" },
        };
        g_script_7e0.clear();
        g_header = "7DF";
        CHECK(elm327_test_init(), "init tem que fechar mesmo sem resposta em 7E0");
        CHECK(!elm327_test_physical(), "sem resposta em 7E0 o polling tem que ficar em broadcast");
        CHECK(g_header == "7DF", "cabecalho final %s, esperava 7DF", g_header.c_str());
        CHECK(elm327_test_mode() == MULTI, "broadcast nao leva contagem de respostas (modo %d)", elm327_test_mode());

        g_script["01340B2305420F\r"] = F20_MULTI;   /* rapidas + as 3 primeiras lentas do plano */
        CHECK(elm327_test_poll(), "em broadcast a leitura tem que chegar: %s", g_last_cmd.c_str());
        CHECK(g_last_cmd == "01340B2305420F\r", "broadcast sem digito de contagem: %s", g_last_cmd.c_str());

        /* Aprendizado: o cambio tambem responde quando o pedido leva o 0x5C.
         * Duas voltas do rodizio (4 pedidos) e a contagem vira 2. */
        g_script["01340B235C5233\r"] = std::string(F20_MULTI).insert(0, "415C50\r");
        CHECK(elm327_test_parse_msgs(g_script["01340B235C5233\r"].c_str()) == 2, "duas ECUs, duas mensagens");
        for (int i = 0; i < 3; i++) elm327_test_poll();
        CHECK(elm327_test_mode() == MULTI_COUNT && elm327_test_count() == 2,
              "aprendeu 2 ECUs: modo %d, contagem %d", elm327_test_mode(), elm327_test_count());
        g_script["01340B2305420F2\r"] = F20_MULTI;
        CHECK(elm327_test_poll() && g_last_cmd == "01340B2305420F2\r",
              "pedido com contagem aprendida: %s", g_last_cmd.c_str());
    }

    printf("== init: DME responde em 7E0 ==\n");
    {
        g_script_7e0 = g_script;
        g_header = "7DF";
        CHECK(elm327_test_init(), "init");
        CHECK(elm327_test_physical() && g_header == "7E0", "com resposta em 7E0 fica so na DME");
        CHECK(elm327_test_mode() == MULTI_COUNT, "so a DME: contagem de respostas (modo %d)", elm327_test_mode());
    }

    printf(g_fail == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d FALHA(S)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
