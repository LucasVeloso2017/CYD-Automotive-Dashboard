#include "elm327_gateway.h"

#include "bt_gateway.h"
#include "dtc_data.h"
#include "vehicle_data.h"

#include <Arduino.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

extern "C" {
    vehicle_data_t vehicle = {};
}

static volatile bool _ready = false;
static volatile bool _init_pending = false;
static uint8_t _pid_index = 0;
static uint8_t _oil_fail_count = 0;
static int8_t _lambda_source_idx = -1;
static bool _lambda_slot = false;

static volatile bool _task_run = false;
static TaskHandle_t _task = NULL;

/* 320 B: um modo 03 com varios codigos chega em multi-frame (varias linhas
 * "N:...") e nao cabia nos 160 B originais. */
static char _resp_buf[320];
static size_t _resp_len = 0;

/* ---- DTC ---------------------------------------------------------------- */
#define DTC_REFRESH_MS  60000UL

static dtc_snapshot_t _dtc = {};
static volatile bool _dtc_request = false;
static uint32_t _dtc_last_ms = 0;
static bool _is_can = true;     /* ATDPN >= 6; muda o formato do 43/47/4A */

static const char *OBD2_CMDS[] = {
    "0105\r", "0142\r", "010B\r", "0123\r", "010F\r", "015C\r",
};
static const uint8_t OBD2_PID[] = { 0x05, 0x42, 0x0B, 0x23, 0x0F, 0x5C };
static const uint8_t OBD2_CMD_COUNT = sizeof(OBD2_CMDS) / sizeof(OBD2_CMDS[0]);

static const struct {
    const char *cmd;
    uint8_t pid;
} LAMBDA_SOURCES[] = {
    { "0144\r", 0x44 },
    { "0134\r", 0x34 },
    { "0124\r", 0x24 },
};
static const uint8_t LAMBDA_SOURCE_COUNT = sizeof(LAMBDA_SOURCES) / sizeof(LAMBDA_SOURCES[0]);

static void flush_serial(void)
{
    while (bt_serial_available() > 0) {
        (void)bt_serial_read();
    }
}

static bool read_response(unsigned long timeout_ms)
{
    _resp_len = 0;
    unsigned long start = millis();

    while (millis() - start < timeout_ms) {
        while (bt_serial_available() > 0) {
            char c = (char)bt_serial_read();
            if (_resp_len < sizeof(_resp_buf) - 1) {
                _resp_buf[_resp_len++] = c;
            }
            if (c == '>') {
                _resp_buf[_resp_len] = '\0';
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    _resp_buf[_resp_len] = '\0';
    return _resp_len > 0;
}

static bool send_cmd(const char *cmd, unsigned long timeout_ms)
{
    flush_serial();
    bt_serial_print(cmd);
    return read_response(timeout_ms);
}

static const char *skip_to_hex(const char *p)
{
    while (*p && !isxdigit((unsigned char)*p)) p++;
    return p;
}

static bool parse_hex_byte(const char *p, uint8_t *out)
{
    p = skip_to_hex(p);
    if (!p[0] || !p[1] || !isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) return false;
    char tmp[3] = { p[0], p[1], '\0' };
    *out = (uint8_t)strtoul(tmp, NULL, 16);
    return true;
}

static bool parse_pid_response(const char *resp, uint8_t pid, uint8_t *a, uint8_t *b)
{
    const size_t len = strlen(resp);

    for (size_t i = 0; i + 3 < len; i++) {
        if (resp[i] != '4' || resp[i + 1] != '1') continue;

        const char *p = skip_to_hex(resp + i + 2);
        uint8_t rsp_pid = 0;
        if (!parse_hex_byte(p, &rsp_pid)) continue;
        if (rsp_pid != pid) continue;

        p += 2;
        uint8_t byte_a = 0, byte_b = 0;
        if (!parse_hex_byte(p, &byte_a)) continue;
        p += 2;
        if (*skip_to_hex(p) && parse_hex_byte(p, &byte_b)) {
            *a = byte_a;
            *b = byte_b;
        } else {
            *a = byte_a;
            *b = 0;
        }
        return true;
    }
    return false;
}

static bool response_has_error(void)
{
    return strstr(_resp_buf, "NO DATA") || strstr(_resp_buf, "ERROR") || strstr(_resp_buf, "UNABLE");
}

static bool apply_lambda(uint8_t a, uint8_t b)
{
    uint16_t raw = (uint16_t)((a << 8) | b);
    if (raw == 0) return false;

    float lambda = (float)raw / 32768.0f;
    if (lambda < 0.5f || lambda > 2.0f) return false;

    vehicle.gauges.lambda = lambda;
    return true;
}

static void notify_gauge_update(void)
{
    vehicle.gauges.data_rev++;
}

static bool try_lambda_source(uint8_t idx)
{
    if (idx >= LAMBDA_SOURCE_COUNT) return false;
    if (!send_cmd(LAMBDA_SOURCES[idx].cmd, ELM327_CMD_TIMEOUT_MS)) return false;
    if (response_has_error()) return false;

    uint8_t a = 0, b = 0;
    if (!parse_pid_response(_resp_buf, LAMBDA_SOURCES[idx].pid, &a, &b)) return false;
    return apply_lambda(a, b);
}

static bool poll_lambda(void)
{
    if (_lambda_source_idx >= 0 && try_lambda_source((uint8_t)_lambda_source_idx)) {
        vehicle.gauges.valid = true;
        notify_gauge_update();
        return true;
    }

    _lambda_source_idx = -1;
    for (uint8_t i = 0; i < LAMBDA_SOURCE_COUNT; i++) {
        if (try_lambda_source(i)) {
            _lambda_source_idx = (int8_t)i;
            vehicle.gauges.valid = true;
            notify_gauge_update();
            return true;
        }
    }
    return false;
}

static void apply_pid(uint8_t pid, uint8_t a, uint8_t b)
{
    switch (pid) {
    case 0x05:
        vehicle.gauges.water_temp_c = (float)a - 40.0f;
        break;
    case 0x42:
        vehicle.gauges.battery_v = (float)(a * 256 + b) / 1000.0f;
        break;
    case 0x0B:
        vehicle.gauges.boost_bar = ((float)a - 101.325f) / 100.0f;
        break;
    case 0x23:
        vehicle.gauges.comb_pressure_bar = (float)(a * 256 + b) / 10.0f;
        break;
    case 0x0F:
        vehicle.gauges.iat_c = (float)a - 40.0f;
        break;
    case 0x5C:
        vehicle.gauges.oil_temp_c = (float)a - 40.0f;
        _oil_fail_count = 0;
        break;
    default:
        break;
    }
    vehicle.gauges.valid = true;
    notify_gauge_update();
}

static bool poll_pid(uint8_t idx)
{
    if (idx >= OBD2_CMD_COUNT) return false;

    uint8_t pid = OBD2_PID[idx];
    if (!send_cmd(OBD2_CMDS[idx], ELM327_CMD_TIMEOUT_MS)) return false;

    if (response_has_error()) {
        if (pid == 0x5C) {
            _oil_fail_count++;
            if (_oil_fail_count >= 3 && vehicle.gauges.water_temp_c > 0.0f) {
                vehicle.gauges.oil_temp_c = vehicle.gauges.water_temp_c + 10.0f;
                vehicle.gauges.valid = true;
                notify_gauge_update();
            }
        }
        return false;
    }

    uint8_t a = 0, b = 0;
    if (parse_pid_response(_resp_buf, pid, &a, &b)) {
        apply_pid(pid, a, b);
        return true;
    }
    return false;
}

/* Junta o payload hex de uma resposta, descartando prompt, "SEARCHING",
 * a linha de tamanho (3 hex) e os prefixos "N:" do multi-frame CAN.
 * Com keep_lines, cada linha vira um grupo separado por '|': em K-line
 * cada linha e uma resposta completa e nao pode ser emendada na seguinte. */
static size_t collect_hex(const char *resp, char *out, size_t cap, bool keep_lines)
{
    size_t n = 0;
    const char *line = resp;
    while (*line) {
        const char *end = line;
        while (*end && *end != '\r' && *end != '\n') end++;

        /* copia a linha sem espacos */
        char buf[96]; size_t bl = 0;
        for (const char *c = line; c < end && bl < sizeof(buf) - 1; c++)
            if (!isspace((unsigned char)*c) && *c != '>') buf[bl++] = *c;
        buf[bl] = '\0';

        bool all_hex = bl > 0;
        for (size_t i = 0; i < bl; i++) if (!isxdigit((unsigned char)buf[i])) { all_hex = false; break; }

        const char *payload = NULL;
        if (bl >= 2 && isxdigit((unsigned char)buf[0]) && buf[1] == ':') payload = buf + 2;   /* "0:..." */
        else if (all_hex && bl != 3) payload = buf;                                          /* "43..." (3 hex = tamanho) */

        if (payload) {
            if (keep_lines && n > 0 && n < cap - 1) out[n++] = '|';
            for (const char *c = payload; *c && n < cap - 1; c++)
                if (isxdigit((unsigned char)*c)) out[n++] = (char)toupper((unsigned char)*c);
        }
        line = (*end) ? end + 1 : end;
    }
    out[n] = '\0';
    return n;
}

static int hexval(char c)
{
    return isdigit((unsigned char)c) ? c - '0' : (toupper((unsigned char)c) - 'A' + 10);
}

static uint8_t hex_byte_at(const char *h, size_t i)
{
    return (uint8_t)((hexval(h[i]) << 4) | hexval(h[i + 1]));
}

static void dtc_add(uint16_t *list, uint8_t *n, uint16_t code)
{
    if (code == 0 || *n >= DTC_MAX_CODES) return;
    for (uint8_t i = 0; i < *n; i++) if (list[i] == code) return;
    list[(*n)++] = code;
}

/* Modo 03/07/0A. CAN: "4N cc d1d2 d1d2 ..." com cc = contagem por ECU.
 * K-line: "4N d1d2 d1d2 d1d2" (3 pares por linha, 0000 = vazio). */
static bool read_dtc_list(const char *cmd, uint8_t mode_rsp, uint16_t *list, uint8_t *n)
{
    *n = 0;
    if (!send_cmd(cmd, 2500)) return false;
    if (response_has_error()) return strstr(_resp_buf, "NO DATA") != NULL;   /* NO DATA = lista vazia, nao erro */

    char hex[256];
    size_t len = collect_hex(_resp_buf, hex, sizeof(hex), !_is_can);
    size_t i = 0;
    while (i + 2 <= len) {
        if (hex[i] == '|') { i++; continue; }
        if (hex_byte_at(hex, i) != mode_rsp) { i += 2; continue; }
        i += 2;
        if (_is_can) {
            if (i + 2 > len) break;
            uint8_t cnt = hex_byte_at(hex, i); i += 2;
            for (uint8_t k = 0; k < cnt && i + 4 <= len; k++, i += 4)
                dtc_add(list, n, (uint16_t)((hex_byte_at(hex, i) << 8) | hex_byte_at(hex, i + 2)));
        } else {
            /* ate 3 pares na linha; 0000 encerra; '|' e a proxima linha */
            for (uint8_t k = 0; k < 3 && i + 4 <= len && hex[i] != '|'; k++, i += 4) {
                uint16_t code = (uint16_t)((hex_byte_at(hex, i) << 8) | hex_byte_at(hex, i + 2));
                if (code == 0) break;
                dtc_add(list, n, code);
            }
        }
    }
    return true;
}

/* Modo 02: "42 PID 00 A B" (frame 0). */
static bool read_ff_pid(uint8_t pid, uint8_t *a, uint8_t *b)
{
    char cmd[10];
    snprintf(cmd, sizeof(cmd), "02%02X00\r", pid);
    if (!send_cmd(cmd, 2000) || response_has_error()) return false;

    char hex[128];
    size_t len = collect_hex(_resp_buf, hex, sizeof(hex), false);
    for (size_t i = 0; i + 8 <= len; i += 2) {
        if (hex_byte_at(hex, i) == 0x42 && hex_byte_at(hex, i + 2) == pid && hex_byte_at(hex, i + 4) == 0x00) {
            *a = hex_byte_at(hex, i + 6);
            *b = (i + 10 <= len) ? hex_byte_at(hex, i + 8) : 0;
            return true;
        }
    }
    return false;
}

static void read_dtcs(void)
{
    dtc_snapshot_t d = {};

    /* PID 01: MIL e contagem */
    if (send_cmd("0101\r", 2000) && !response_has_error()) {
        uint8_t a = 0, b = 0;
        if (parse_pid_response(_resp_buf, 0x01, &a, &b)) {
            d.mil = (a & 0x80) != 0;
            d.ecu_count = a & 0x7F;
        }
    }

    bool ok = read_dtc_list("03\r", 0x43, d.stored, &d.n_stored);
    read_dtc_list("07\r", 0x47, d.pending, &d.n_pending);
    read_dtc_list("0A\r", 0x4A, d.permanent, &d.n_permanent);   /* opcional em carros antigos */

    if (d.n_stored > 0) {
        uint8_t a = 0, b = 0;
        if (read_ff_pid(0x02, &a, &b)) {
            d.ff.dtc = (uint16_t)((a << 8) | b);
            d.ff.valid = d.ff.dtc != 0;
        }
        if (d.ff.valid) {
            if (read_ff_pid(0x0C, &a, &b)) d.ff.rpm = (uint16_t)(((a << 8) | b) / 4);
            if (read_ff_pid(0x0D, &a, &b)) d.ff.speed_kmh = a;
            if (read_ff_pid(0x05, &a, &b)) d.ff.coolant_c = (int16_t)a - 40;
            if (read_ff_pid(0x04, &a, &b)) d.ff.load_pct = (uint8_t)(a * 100 / 255);
            if (read_ff_pid(0x11, &a, &b)) d.ff.throttle_pct = (uint8_t)(a * 100 / 255);
            if (read_ff_pid(0x0B, &a, &b)) d.ff.map_kpa = a;
        }
    }

    d.valid = ok;
    d.read_ms = millis();
    _dtc = d;
    _dtc_last_ms = d.read_ms;

    char code[6];
    Serial.printf("[DTC] MIL %s, ECU informa %u | armazenados %u, pendentes %u, permanentes %u%s\n",
                  d.mil ? "ACESA" : "apagada", d.ecu_count, d.n_stored, d.n_pending, d.n_permanent,
                  ok ? "" : " (leitura incompleta)");
    for (uint8_t i = 0; i < d.n_stored; i++) { dtc_format(d.stored[i], code); Serial.printf("[DTC]   armazenado %s\n", code); }
    for (uint8_t i = 0; i < d.n_pending; i++) { dtc_format(d.pending[i], code); Serial.printf("[DTC]   pendente   %s\n", code); }
    if (d.ff.valid) {
        dtc_format(d.ff.dtc, code);
        Serial.printf("[DTC]   freeze frame %s: %u rpm, %u km/h, agua %d C, carga %u%%, TPS %u%%, MAP %u kPa\n",
                      code, d.ff.rpm, d.ff.speed_kmh, d.ff.coolant_c, d.ff.load_pct, d.ff.throttle_pct, d.ff.map_kpa);
    }
}

extern "C" void dtc_format(uint16_t raw, char *out)
{
    static const char sys[4] = { 'P', 'C', 'B', 'U' };
    out[0] = sys[(raw >> 14) & 0x03];
    out[1] = (char)('0' + ((raw >> 12) & 0x03));
    static const char hexd[] = "0123456789ABCDEF";
    out[2] = hexd[(raw >> 8) & 0x0F];
    out[3] = hexd[(raw >> 4) & 0x0F];
    out[4] = hexd[raw & 0x0F];
    out[5] = '\0';
}

const dtc_snapshot_t *elm327_dtc(void)
{
    return &_dtc;
}

#ifdef ELM327_HOST_TEST
/* Teste no PC (preview/test_dtc.cpp): injeta respostas ELM gravadas e
 * confere o parser sem hardware. */
void elm327_test_read_dtcs(bool is_can)
{
    _is_can = is_can;
    read_dtcs();
}
#endif

void elm327_request_dtc_read(void)
{
    _dtc_request = true;
}

static bool run_init_sequence(void)
{
    Serial.println("[ELM327] Initializing...");

    send_cmd("ATZ\r", 2500);
    vTaskDelay(pdMS_TO_TICKS(400));
    send_cmd("ATE0\r", 800);
    send_cmd("ATL0\r", 800);
    send_cmd("ATS0\r", 800);
    send_cmd("ATH0\r", 800);
    send_cmd("ATSP0\r", 1500);

    if (!send_cmd("0100\r", 2500)) {
        Serial.println("[ELM327] ECU timeout");
        return false;
    }
    if (!strstr(_resp_buf, "41 00") && !strstr(_resp_buf, "4100")) {
        Serial.println("[ELM327] ECU not responding");
        return false;
    }

    /* ATDPN: "A6" = auto, protocolo 6 (CAN 11 bit 500k). >= 6 e CAN, e o
     * formato das respostas de DTC depende disso. */
    _is_can = true;
    if (send_cmd("ATDPN\r", 1000)) {
        const char *p = _resp_buf;
        while (*p && !isxdigit((unsigned char)*p)) p++;
        if (*p == 'A' || *p == 'a') p++;
        if (isxdigit((unsigned char)*p)) _is_can = hexval(*p) >= 6;
        Serial.printf("[ELM327] Protocolo %c (%s)\n", *p ? *p : '?', _is_can ? "CAN" : "K-line");
    }

    _dtc_request = true;    /* primeira leitura de DTC logo que a sessao abre */
    Serial.println("[ELM327] Ready");
    return true;
}

static void elm327_task(void *param)
{
    (void)param;
    while (_task_run) {
        if (!bt_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (_init_pending) {
            _init_pending = false;
            _ready = run_init_sequence();
            continue;
        }

        if (!_ready) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (_dtc_request || (millis() - _dtc_last_ms >= DTC_REFRESH_MS)) {
            _dtc_request = false;
            read_dtcs();            /* ~10 comandos, ~2 s; a rotacao de PIDs retoma em seguida */
            continue;
        }

        if (_lambda_slot) {
            _lambda_slot = false;
            poll_lambda();
        } else {
            poll_pid(_pid_index);
            _pid_index++;
            if (_pid_index >= OBD2_CMD_COUNT) {
                _pid_index = 0;
                _lambda_slot = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }
    _task = NULL;
    vTaskDelete(NULL);
}

static void ensure_task(void)
{
    if (_task != NULL) return;
    _task_run = true;
    if (xTaskCreate(elm327_task, "elm327", 8192, NULL, 1, &_task) != pdPASS) {
        _task_run = false;
        Serial.println("[ELM327] Task create failed");
    }
}

void elm327_stop_task(void)
{
    if (_task == NULL) return;
    _task_run = false;
    /* a task sai no proximo loop; no pior caso espera um comando ELM
     * estourar o timeout (1,2 s). Sem esperar, um ensure_task() logo em
     * seguida veria _task != NULL e nao recriaria. */
    for (int i = 0; i < 200 && _task != NULL; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (_task != NULL) Serial.println("[ELM327] task nao saiu a tempo");
}

void elm327_init_session(void)
{
    _init_pending = true;
    _ready = false;
    _pid_index = 0;
    _lambda_slot = false;
    _lambda_source_idx = -1;
    _oil_fail_count = 0;
    memset(&vehicle.gauges, 0, sizeof(vehicle.gauges));
    ensure_task();
}

void elm327_reset_session(void)
{
    _ready = false;
    _init_pending = false;
    _pid_index = 0;
    _lambda_slot = false;
    _lambda_source_idx = -1;
    memset(&vehicle.gauges, 0, sizeof(vehicle.gauges));
}

bool elm327_is_ready(void) { return _ready; }

void elm327_loop(void)
{
    /* polling runs in background task */
}
