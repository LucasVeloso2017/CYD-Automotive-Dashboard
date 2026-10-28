// Teste do lado CYD do fio, no PC: compila o src/net/link.cpp de verdade
// contra uma serial falsa e confere cada quadro como o Wemos conferiria.
//
//   cd preview && make -f test_wire.mk
//
// O link.cpp monta os quadros em fluxo, com o XOR calculado pedaco a pedaco
// enquanto escreve. Um erro ali (um pedaco fora do XOR, um escape errado) so
// apareceria no carro como "quadros ruins" no log do Wemos. Aqui ele aparece
// antes. Os JSON de S e D saem em dist/wire_json.txt e o make os valida com o
// parser de JSON do Python.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Arduino.h"
#include "link.h"
#include "link_proto.h"
#include "bt_gateway.h"
#include "dtc_data.h"
#include "elm327_gateway.h"
#include "vehicle_data.h"
#include "../src/ui/ui_mock_data.h"

HardwareSerial Serial;
HardwareSerial Serial2;
static unsigned long g_ms = 0;
unsigned long millis(void) { return g_ms; }

/* ---- o resto do firmware, no minimo necessario -------------------------- */
vehicle_data_t vehicle;
static bool g_bt = true, g_ready = true;
static dtc_snapshot_t g_dtc;
static dtc_op_t g_op = DTC_OP_IDLE;
static int g_reads = 0, g_clears = 0;

extern "C" {
bool bt_is_connected(void) { return g_bt; }
bool elm327_is_ready(void) { return g_ready; }
void elm327_dtc_copy(dtc_snapshot_t *out) { *out = g_dtc; }
dtc_op_t elm327_dtc_op(void) { return g_op; }
dtc_clear_result_t elm327_dtc_clear_result(uint8_t *nrc) { if (nrc) *nrc = 0; return DTC_CLEAR_NONE; }
void elm327_request_dtc_read(void) { g_reads++; }
void elm327_request_dtc_clear(void) { g_clears++; }
void dtc_format(uint16_t raw, char *out)
{
    static const char L[] = "PCBU";
    snprintf(out, 6, "%c%04X", L[raw >> 14], raw & 0x3FFF);
}
void ui_mock_data_session_max(vehicle_gauge_data_t *out)
{
    memset(out, 0, sizeof(*out));
    out->boost_bar = 1.21f;
    out->oil_temp_c = NAN;              /* sem 0x5C: tem que virar null */
}
}

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("  FALHOU: " __VA_ARGS__); printf("\n"); } } while (0)

struct Frames { std::vector<std::string> S, G, D; int bad = 0; };

/* Recorta o que o CYD mandou em linhas e confere cada uma como o Wemos faria. */
static Frames drain(void)
{
    Frames f;
    size_t start = 0;
    std::string &tx = Serial2.tx;
    for (size_t i = 0; i < tx.size(); i++) {
        if (tx[i] != '\n') continue;
        std::string line = tx.substr(start, i - start);
        start = i + 1;
        std::vector<char> b(line.begin(), line.end());
        b.push_back('\0');
        if (link_verify(b.data(), line.size()) <= 0) { f.bad++; continue; }
        const std::string body(b.data());
        if (body[0] == LINK_STATE) f.S.push_back(body.substr(1));
        else if (body[0] == LINK_SAMPLE) f.G.push_back(body);
        else if (body[0] == LINK_DTC) f.D.push_back(body.substr(1));
    }
    tx.erase(0, start);
    return f;
}

static void inject(const char *content)
{
    char b[200];
    snprintf(b, sizeof(b), "%s", content);
    link_seal(b, strlen(b), sizeof(b));
    Serial2.rx += b;
}

static void step(unsigned long ms) { g_ms += ms; link_loop(); }

int main(void)
{
    Serial.quiet = true;
    FILE *json = fopen("dist/wire_json.txt", "w");

    vehicle.gauges.valid = true;
    vehicle.gauges.boost_bar = 0.85f;
    vehicle.gauges.lambda = 0.987f;
    vehicle.gauges.water_temp_c = 90.0f;
    vehicle.gauges.battery_v = 13.8f;
    vehicle.gauges.comb_pressure_bar = 211.7f;
    vehicle.gauges.iat_c = -3.0f;
    vehicle.gauges.oil_temp_c = NAN;

    g_dtc.valid = true;
    g_dtc.read_ms = 4242;
    g_dtc.mil = true;
    g_dtc.n_stored = 2; g_dtc.stored[0] = 0x0301; g_dtc.stored[1] = 0x0171;
    g_dtc.n_pending = 1; g_dtc.pending[0] = 0x0420;
    g_dtc.n_permanent = 1; g_dtc.permanent[0] = 0x1518;
    g_dtc.ff.valid = true; g_dtc.ff.dtc = 0x0301; g_dtc.ff.rpm = 6210; g_dtc.ff.coolant_c = -5;

    link_init();

    printf("== a cada LINK_STATE_MS: S e G validos ==\n");
    step(LINK_STATE_MS);
    Frames f = drain();
    CHECK(f.bad == 0, "%d quadro(s) com checksum errado", f.bad);
    CHECK(f.S.size() == 1 && f.G.size() == 1, "esperava 1 S e 1 G, vieram %zu e %zu", f.S.size(), f.G.size());
    CHECK(f.D.size() == 1, "a primeira lista de DTC devia sair junto, vieram %zu", f.D.size());
    for (auto &s : f.S) fprintf(json, "%s\n", s.c_str());
    for (auto &d : f.D) fprintf(json, "%s\n", d.c_str());
    if (!f.S.empty()) {
        CHECK(f.S[0].find("\"oil\":null") != std::string::npos, "oleo NaN devia virar null: %s", f.S[0].c_str());
        CHECK(f.S[0].find("\"link\":true") != std::string::npos, "sessao ativa devia sair como link:true");
    }
    if (!f.D.empty()) {
        CHECK(f.D[0].find("\"c\":\"P0301\"") != std::string::npos, "P0301 nao apareceu na lista");
        CHECK(f.D[0].find("\"coolant\":-5") != std::string::npos, "freeze frame negativo se perdeu");
    }

    printf("== a amostra G volta igual ao que esta no painel ==\n");
    link_sample_t smp;
    CHECK(!f.G.empty() && link_sample_parse(f.G[0].c_str(), &smp) == 0, "G ilegivel");
    CHECK(smp.valid && std::fabs(smp.v[LINK_F_BOOST] - 0.85f) < 0.001f && std::isnan(smp.v[LINK_F_OIL]),
          "G com valores errados");

    printf("== cadencia do estado: um a cada LINK_STATE_MS, nunca antes ==\n");
    step(LINK_STATE_MS / 2);
    CHECK(drain().S.empty(), "meio periodo depois nao devia sair estado");
    step(LINK_STATE_MS / 2);
    CHECK(drain().S.size() == 1, "no periodo cheio devia sair um estado");
    {
        /* loop() a cada 50 ms (divide o periodo): exatamente 10 em 10 periodos */
        size_t states = 0;
        for (int i = 0; i < 50; i++) { step(50); states += drain().S.size(); }
        CHECK(states == 10, "loop a 50 ms: em 10 periodos deviam sair 10 estados, sairam %zu", states);

        /* loop() a cada 62 ms (nao divide): sem deriva continua 1 por periodo,
         * com "_last = now" cada periodo virava 310 ms e saiam 8 */
        states = 0;
        for (int i = 0; i < 81; i++) { step(62); states += drain().S.size(); }
        CHECK(states >= 19 && states <= 21, "loop a 62 ms por ~5 s: esperava ~20 estados, sairam %zu", states);

        /* loop() travada 2 s (ex.: algo bloqueou): retoma sem rajada */
        step(2000);
        CHECK(drain().S.size() == 1, "depois de um atraso longo devia sair um estado, nao uma rajada");
        step(LINK_STATE_MS);
        CHECK(drain().S.size() == 1, "e a cadencia normal segue dali");
    }

    printf("== lista de DTC so sai quando muda ==\n");
    step(500); step(500);
    f = drain();
    CHECK(f.D.empty(), "lista repetida sem mudanca: %zu", f.D.size());
    CHECK(f.S.size() == 2, "um estado por chamada espacada, vieram %zu", f.S.size());
    g_dtc.read_ms = 9000;
    g_op = DTC_OP_READING;
    step(500);
    f = drain();
    CHECK(f.D.empty(), "durante a leitura o snapshot esta pela metade: nao devia sair");
    g_op = DTC_OP_IDLE;
    step(500);
    f = drain();
    CHECK(f.D.size() == 1, "leitura nova terminada devia gerar uma lista, vieram %zu", f.D.size());

    printf("== lista reenviada de tempos em tempos, mesmo sem mudanca ==\n");
    {
        int seen = 0;
        for (unsigned long t = 0; t < LINK_DTC_REFRESH_MS + 1000; t += 500) {
            step(500);
            seen += (int)drain().D.size();
        }
        CHECK(seen == 1, "em %d ms sem mudanca devia sair 1 lista de reforco, sairam %d",
              LINK_DTC_REFRESH_MS + 1000, seen);
    }

    printf("== Wemos se anuncia: rede no QR e lista reenviada ==\n");
    CHECK(!link_portal_alive(), "sem W o Wemos nao esta vivo");
    inject("WCarDash-ABCD\tdashA1ABCD\thttp://192.168.4.1\t1");
    step(500);
    f = drain();
    CHECK(link_portal_alive(), "W valido devia marcar o Wemos como vivo");
    CHECK(!strcmp(link_portal_ssid(), "CarDash-ABCD") && link_portal_clients() == 1, "W mal lido");
    CHECK(!strcmp(link_portal_qr_payload(), "WIFI:T:WPA;S:CarDash-ABCD;P:dashA1ABCD;;"),
          "QR errado: %s", link_portal_qr_payload());
    CHECK(f.D.size() == 1, "Wemos que aparece precisa receber a lista de novo");
    step(LINK_STALE_MS);
    CHECK(!link_portal_alive(), "sem W por LINK_STALE_MS o Wemos devia ser dado como sumido");

    printf("== pedidos da pagina passam pelas travas ==\n");
    inject("R"); step(500);
    CHECK(g_reads == 1, "ler com sessao ativa e ociosa devia passar");
    inject("X"); step(500);
    CHECK(g_clears == 1, "apagar com codigos lidos devia passar");
    g_bt = false;
    inject("R"); inject("X"); step(500);
    CHECK(g_reads == 1 && g_clears == 1, "sem ELM327 nenhum pedido pode ficar pendurado");
    g_bt = true;
    g_dtc.n_stored = 0; g_dtc.n_pending = 0;
    inject("X"); step(500);
    CHECK(g_clears == 1, "apagar sem codigo armazenado/pendente nao faz sentido");
    g_op = DTC_OP_CLEARING;
    inject("R"); step(500);
    CHECK(g_reads == 1, "com operacao em andamento nao aceita outra");
    g_op = DTC_OP_IDLE;

    printf("== quadro corrompido e ignorado ==\n");
    Serial2.rx += "R*00\n";                 /* checksum errado */
    Serial2.rx += "Xlixo sem checksum\n";
    step(500);
    CHECK(g_reads == 1 && g_clears == 1, "quadro invalido nao pode virar comando");
    std::string huge(400, 'R');              /* maior que o buffer de recepcao */
    Serial2.rx += huge + "\n";
    inject("R"); step(500);
    CHECK(g_reads == 2, "depois de uma linha gigante descartada, o proximo quadro valido tem que passar");

    printf("== tudo o que saiu, conferido ==\n");
    for (int i = 0; i < 20; i++) step(500);
    f = drain();
    CHECK(f.bad == 0, "%d quadro(s) ruins no fluxo longo", f.bad);
    for (auto &s : f.S) fprintf(json, "%s\n", s.c_str());

    fclose(json);
    printf(g_fail ? "\n%d verificacao(oes) falharam\n" : "\nok\n", g_fail);
    return g_fail ? 1 : 0;
}
