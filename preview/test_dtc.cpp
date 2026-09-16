// Teste do parser de DTC do elm327_gateway no PC, sem hardware.
//
//   cd preview && make -f test_dtc.mk
//
// Cada caso injeta respostas ELM327 gravadas (ATE0/ATL0/ATS0/ATH0: sem eco,
// sem linefeed, sem espacos, sem cabecalhos) comando a comando e confere o
// snapshot resultante.

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bt_gateway.h"
#include "elm327_gateway.h"
#include "dtc_data.h"

void elm327_test_read_dtcs(bool is_can);   /* linkage C++, como no .cpp */

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
static std::string g_rx;
static size_t g_rx_pos = 0;

extern "C" {
int bt_serial_available(void) { return (int)(g_rx.size() - g_rx_pos); }
int bt_serial_read(void) { return g_rx_pos < g_rx.size() ? (unsigned char)g_rx[g_rx_pos++] : -1; }
void bt_serial_print(const char *cmd) {
    auto it = g_script.find(cmd);
    g_rx = (it != g_script.end()) ? it->second : "NO DATA\r\r>";
    g_rx_pos = 0;
}
}

/* ---- harness ----------------------------------------------------------- */
static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("  FALHOU: " __VA_ARGS__); printf("\n"); } } while (0)

static std::string fmt(uint16_t raw) { char b[6]; dtc_format(raw, b); return b; }

static void run(const char *name, bool is_can, std::map<std::string, std::string> script) {
    printf("== %s ==\n", name);
    g_script = std::move(script);
    elm327_test_read_dtcs(is_can);
}

int main() {
    /* 1. CAN, single-frame, 2 armazenados, MIL acesa, freeze frame completo */
    run("CAN single-frame + freeze frame", true, {
        {"0101\r", "4101820700E0\r\r>"},          /* 0x82: MIL + 2 codigos */
        {"03\r",   "430201330271\r\r>"},          /* P0133, P0271 */
        {"07\r",   "4700\r\r>"},
        {"0A\r",   "4A00\r\r>"},
        {"020200\r", "4202000133\r\r>"},          /* DTC do freeze frame: P0133 */
        {"020C00\r", "420C001AF8\r\r>"},          /* rpm 0x1AF8/4 = 1726 */
        {"020D00\r", "420D0084\r\r>"},            /* 132 km/h */
        {"020500\r", "42050090\r\r>"},            /* 0x90-40 = 104 C */
        {"020400\r", "420400FF\r\r>"},            /* 100% */
        {"021100\r", "421100FA\r\r>"},            /* 98% */
        {"020B00\r", "420B00D2\r\r>"},            /* 210 kPa */
    });
    {
        const dtc_snapshot_t *d = elm327_dtc();
        CHECK(d->valid, "valid");
        CHECK(d->mil && d->ecu_count == 2, "MIL/contagem: mil=%d cnt=%u", d->mil, d->ecu_count);
        CHECK(d->n_stored == 2, "n_stored=%u", d->n_stored);
        CHECK(fmt(d->stored[0]) == "P0133" && fmt(d->stored[1]) == "P0271", "codigos %s %s", fmt(d->stored[0]).c_str(), fmt(d->stored[1]).c_str());
        CHECK(d->n_pending == 0 && d->n_permanent == 0, "pendentes/permanentes vazios");
        CHECK(d->ff.valid && fmt(d->ff.dtc) == "P0133", "ff dtc %s", fmt(d->ff.dtc).c_str());
        CHECK(d->ff.rpm == 1726, "ff rpm %u", d->ff.rpm);
        CHECK(d->ff.speed_kmh == 132, "ff speed %u", d->ff.speed_kmh);
        CHECK(d->ff.coolant_c == 104, "ff coolant %d", d->ff.coolant_c);
        CHECK(d->ff.load_pct == 100 && d->ff.throttle_pct == 98 && d->ff.map_kpa == 210, "ff load/tps/map %u %u %u", d->ff.load_pct, d->ff.throttle_pct, d->ff.map_kpa);
    }

    /* 2. CAN multi-frame: 5 codigos (12 bytes) em FF + CF com linha de tamanho */
    run("CAN multi-frame (5 codigos)", true, {
        {"0101\r", "4101850700E0\r\r>"},
        {"03\r",   "00C\r0:430501330271\r1:01000301040000\r\r>"},
        {"07\r",   "NO DATA\r\r>"},
        {"0A\r",   "NO DATA\r\r>"},
        {"020200\r", "4202000133\r\r>"},
        {"020C00\r", "420C000000\r\r>"},
    });
    {
        const dtc_snapshot_t *d = elm327_dtc();
        CHECK(d->valid, "valid");
        CHECK(d->n_stored == 5, "n_stored=%u (esperado 5)", d->n_stored);
        const char *exp[] = {"P0133", "P0271", "P0100", "P0301", "P0400"};
        for (int i = 0; i < 5 && i < d->n_stored; i++)
            CHECK(fmt(d->stored[i]) == exp[i], "codigo %d: %s != %s", i, fmt(d->stored[i]).c_str(), exp[i]);
        CHECK(d->n_pending == 0, "NO DATA no 07 = vazio, nao erro (n=%u)", d->n_pending);
    }

    /* 3. Duas ECUs respondem ao 03: motor sem codigos, cambio com 1 (U0100) */
    run("duas ECUs", true, {
        {"0101\r", "4101010700E0\r\r>"},
        {"03\r",   "4300\r4301C100\r\r>"},
        {"07\r",   "4700\r4700\r\r>"},
        {"0A\r",   "4A00\r\r>"},
        {"020200\r", "4202000000\r\r>"},        /* sem freeze frame */
    });
    {
        const dtc_snapshot_t *d = elm327_dtc();
        CHECK(d->n_stored == 1 && fmt(d->stored[0]) == "U0100", "n=%u %s", d->n_stored, d->n_stored ? fmt(d->stored[0]).c_str() : "-");
        CHECK(!d->ff.valid, "freeze frame invalido quando DTC=0000");
        CHECK(!d->mil, "MIL apagada");
    }

    /* 4. K-line (ISO 9141): 3 pares por linha, 0000 = vazio; codigo C e B */
    run("K-line", false, {
        {"0101\r", "4101830000\r\r>"},
        {"03\r",   "4301334121\r4380350000\r\r>"},   /* P0133, C0121, B0035 */
        {"07\r",   "4300\r\r>"},                     /* (ELM velho ecoa 43 no 07: ignorado por nao ser 47) */
        {"0A\r",   "?\r\r>"},                        /* modo nao suportado */
        {"020200\r", "NO DATA\r\r>"},
    });
    {
        const dtc_snapshot_t *d = elm327_dtc();
        CHECK(d->n_stored == 3, "n_stored=%u (esperado 3)", d->n_stored);
        CHECK(d->n_stored >= 3 && fmt(d->stored[0]) == "P0133" && fmt(d->stored[1]) == "C0121" && fmt(d->stored[2]) == "B0035",
              "codigos %s %s %s", fmt(d->stored[0]).c_str(), fmt(d->stored[1]).c_str(), fmt(d->stored[2]).c_str());
        CHECK(d->n_permanent == 0, "0A nao suportado = vazio");
        CHECK(!d->ff.valid, "sem freeze frame");
    }

    /* 5. Sem codigos, tudo limpo */
    run("carro limpo", true, {
        {"0101\r", "4101000700E0\r\r>"},
        {"03\r",   "4300\r\r>"},
        {"07\r",   "4700\r\r>"},
        {"0A\r",   "4A00\r\r>"},
    });
    {
        const dtc_snapshot_t *d = elm327_dtc();
        CHECK(d->valid && d->n_stored == 0 && d->n_pending == 0 && d->n_permanent == 0 && !d->mil && !d->ff.valid, "tudo zero");
    }

    /* 6. dtc_format cobre os 4 sistemas e digitos hex */
    CHECK(fmt(0x0301) == "P0301", "P0301");
    CHECK(fmt(0x1A2B) == "P1A2B", "P1A2B: %s", fmt(0x1A2B).c_str());
    CHECK(fmt(0x4121) == "C0121", "C0121");
    CHECK(fmt(0x8035) == "B0035", "B0035");
    CHECK(fmt(0xC100) == "U0100", "U0100");

    printf("\n%s (%d falha%s)\n", g_fail ? "FALHOU" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
