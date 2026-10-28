#include "elm327_gateway.h"

#include "bt_gateway.h"
#include "dtc_data.h"
#include "vehicle_data.h"

#include <Arduino.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include "vehicle_profile.h"

extern "C" {
    vehicle_data_t vehicle = {};
}

static volatile bool _ready = false;
static volatile bool _init_pending = false;

static volatile bool _task_run = false;
static TaskHandle_t _task = NULL;

/* 320 B: um modo 03 com varios codigos chega em multi-frame (varias linhas
 * "N:...") e nao cabia nos 160 B originais. */
static char _resp_buf[320];
static size_t _resp_len = 0;

/* ---- DTC ---------------------------------------------------------------- */
#define DTC_CLEAR_TIMEOUT_MS  5000   /* o modo 04 demora mais que uma leitura em varias ECUs */
/* Depois do 04 a DME ainda esta gravando a memoria de falhas. Relendo na hora,
 * o 03 devolve os codigos antigos e a tela parece dizer que nada foi apagado. */
#define DTC_CLEAR_SETTLE_MS   1500

static dtc_snapshot_t _dtc = {};
static volatile bool _dtc_request = false;
static volatile bool _dtc_clear_request = false;
static volatile dtc_op_t _dtc_op = DTC_OP_IDLE;
static volatile dtc_clear_result_t _dtc_clear_result = DTC_CLEAR_NONE;
static volatile uint8_t _dtc_clear_nrc = 0;
static portMUX_TYPE _dtc_mux = portMUX_INITIALIZER_UNLOCKED;   /* a task escreve _dtc, a UI le */
static bool _is_can = true;     /* ATDPN >= 6; muda o formato do 43/47/4A */

/* Pilha da task de polling. Era 8 kB; o caminho mais fundo (leitura de DTC:
 * hex[256] + buf[96] + Serial.printf sem float) fica bem abaixo de 2.5 kB, e
 * cada kB devolvido aqui e um kB a mais para o WiFi do portal. A sobra minima
 * real sai no serial depois do init e de cada leitura de DTC. */
#define ELM327_TASK_STACK  4096

/* Plano de polling da sessao, montado no init a partir do perfil do veiculo e
 * dos bitmaps de PID suportado: so entra PID que o carro declarou.
 *
 * O que manda na taxa de atualizacao e o numero de trocas na serial, nao o de
 * PIDs: cada troca custa a ida e volta pelo Bluetooth, o ELM327 e a ECU, e
 * medido no F20 (pelo ENET) 1 PID ou 6 PIDs num pedido levam os mesmos ~30 ms
 * na DME. Entao cada pedido leva as grandezas rapidas (boost, rampa, lambda)
 * e completa os slots com as lentas em rodizio (vp_next_request). */
#define OBD2_PID_CAP     VP_METRIC_COUNT
#define OBD2_REQ_MAX     6       /* 01 + 6 PIDs = 7 bytes, o maximo de um single frame */

static uint8_t _fast[OBD2_PID_CAP], _n_fast = 0;
static uint8_t _slow[OBD2_PID_CAP], _n_slow = 0;
static vp_sched_t _sched = {};

/* Escada de modos. A sessao comeca no mais rapido que o perfil permite e desce
 * um degrau quando nada util volta (OBD2_DEGRADE_AFTER pedidos seguidos sem
 * resposta ou com multi-frame cortado):
 *   MULTI_COUNT  varios PIDs + contagem de respostas ("...1"), so a DME (7E0):
 *                o ELM327 volta assim que ela responde, sem esperar o timeout;
 *   MULTI        varios PIDs, ELM327 espera o timeout (encurtado pelo ATAT2);
 *     ...e ainda em MULTI, o cabecalho fisico cai para broadcast (7DF): o
 *     gateway do carro pode nao repassar pedido enderecado direto a DME;
 *   PACKED       varios PIDs, mas so o que cabe na resposta de um quadro CAN
 *                (vp_next_request_packed): para o clone que corta resposta
 *                multi-frame no primeiro quadro, mas aceita varios PIDs. A
 *                volta e [lambda] [turbo + rampa] [3 lentas]; em "um por vez"
 *                seria [turbo] [rampa] [lambda] [1 lenta], e o etanol vinha 1
 *                pedido em 24;
 *   SINGLE       um PID por pedido, em broadcast, como era antes de tudo isto.
 * O ultimo degrau e o comportamento antigo, que funcionava: a escada nunca
 * pode terminar num estado pior que ele. */
typedef enum { POLL_MULTI_COUNT = 0, POLL_MULTI, POLL_PACKED, POLL_SINGLE } poll_mode_t;
static const char *const POLL_MODE_NAME[] = { "multi+contagem", "multi", "agrupado", "um por vez" };
#define OBD2_DEGRADE_AFTER  3

static poll_mode_t _mode = POLL_SINGLE;
static uint8_t _bad_streak = 0;
static bool _physical = false;     /* ATSH do perfil ativo; false = broadcast 7DF */

/* Contagem de respostas (o digito no fim do pedido) em MULTI_COUNT. So a DME:
 * 1. Em broadcast ela e aprendida: as primeiras OBD2_LEARN_ROUNDS voltas do
 * rodizio vao sem contagem, e o maior numero de ECUs que respondeu a um pedido
 * vira a contagem. Toda grandeza do plano passa pelo aprendizado, entao a
 * contagem cobre a ECU extra que responde a algum PID lento (o cambio no 0x05,
 * por exemplo). Sem ela, cada pedido em broadcast espera o timeout do ELM327
 * depois da ultima resposta — o que mais custava taxa depois que o F20 se
 * mostrou surdo ao 7E0. */
#define OBD2_LEARN_ROUNDS  2
static uint8_t _count = 0;          /* 0 = sem contagem */
static uint8_t _learn_left = 0;     /* pedidos de aprendizado restantes */
static uint8_t _learn_max = 0;      /* mais ECUs vistas num pedido durante o aprendizado */
static bool    _learned = false;    /* ja aprendeu nesta sessao: nao reaprende depois de cair */

/* Resposta crua dos pedidos que falham, no serial: sem o CYD no USB nao ha
 * outro jeito de saber o que o adaptador devolveu. Limitado por sessao. */
#define OBD2_BAD_LOG_MAX  12
static uint8_t _bad_logs = 0;

/* Taxa medida, para o serial: pedidos completos por janela, e quantas vezes
 * lambda, turbo e etanol foram gravados de fato (um pedido pode voltar sem o
 * PID que levava). */
#define OBD2_RATE_LOG_MS  5000
static uint32_t _rate_t0 = 0;
static uint16_t _rate_n = 0;
static uint16_t _rate_lambda = 0, _rate_boost = 0, _rate_eth = 0;

/* Bytes de resposta de um single frame CAN, sem o 0x41 (PCI + 7 de dado). */
#define OBD2_SF_BUDGET  6

/* Bitmaps de 0100 / 0120 / 0140, na ordem; `_support_have` diz quantos vieram. */
static uint8_t _support[3][4];
static uint8_t _support_have = 0;

/* PID 0x33 (pressao barometrica absoluta, kPa); <= 0 quando o carro nao publica. */
static float _baro_kpa = 0.0f;

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
        vTaskDelay(pdMS_TO_TICKS(1));
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

static void notify_gauge_update(void)
{
    vehicle.gauges.data_rev++;
}

static void apply_lambda(uint16_t raw)
{
    /* 0x34/0x24: razao de equivalencia, 2/65536 por bit. Fora de 0.5..2.0 e
     * corte de combustivel (0xFFFF) ou sonda fria: mantem o ultimo valor. */
    const float lambda = (float)raw / 32768.0f;
    if (lambda < 0.5f || lambda > 1.99f) return;
    vehicle.gauges.lambda = lambda;
}

/* Grava uma grandeza. `d` tem vp_pid_data_len(pid) bytes. Nao avisa a UI: quem
 * chama avisa uma vez por resposta, com todos os PIDs dela ja gravados. */
static void apply_pid(uint8_t pid, const uint8_t *d)
{
    const uint16_t ab = (uint16_t)((d[0] << 8) | (vp_pid_data_len(pid) >= 2 ? d[1] : 0));
    switch (pid) {
    case 0x05:
        vehicle.gauges.water_temp_c = (float)d[0] - 40.0f;
        break;
    case 0x42:
        vehicle.gauges.battery_v = (float)ab / 1000.0f;
        break;
    case 0x0B:
        vehicle.gauges.boost_bar = vp_boost_bar(d[0], _baro_kpa);
        _rate_boost++;
        break;
    case 0x33:
        /* Barometro: entra no boost, nao tem gauge proprio. */
        if (d[0] > 0) _baro_kpa = (float)d[0];
        break;
    case 0x23:
    case 0x59:
        /* 0x23 manometrica, 0x59 absoluta: mesmo fator de 10 kPa por bit. */
        vehicle.gauges.comb_pressure_bar = (float)ab / 10.0f;
        break;
    case 0x0F:
        vehicle.gauges.iat_c = (float)d[0] - 40.0f;
        break;
    case 0x5C:
        vehicle.gauges.oil_temp_c = (float)d[0] - 40.0f;
        break;
    case 0x34:
    case 0x24:
        apply_lambda(ab);
        _rate_lambda++;
        break;
    case 0x52:
        vehicle.gauges.ethanol_pct = (float)d[0] * 100.0f / 255.0f;
        _rate_eth++;
        break;
    default:
        break;
    }
}

/* Uma mensagem de modo 01 ja montada em bytes: "41 PID dados PID dados ...".
 * Sem separador entre os PIDs, o tamanho de cada um diz onde comeca o
 * proximo; PID de tamanho desconhecido encerra a leitura (o resto seria
 * chute). Devolve quantos PIDs foram gravados. */
static uint8_t apply_mode01_message(const uint8_t *m, size_t len)
{
    if (len < 2 || m[0] != 0x41) return 0;
    uint8_t applied = 0;
    size_t i = 1;
    while (i < len) {
        const uint8_t pid = m[i];
        const uint8_t dl = vp_pid_data_len(pid);
        if (dl == 0 || i + 1 + dl > len) break;
        apply_pid(pid, &m[i + 1]);
        applied++;
        i += 1 + (size_t)dl;
    }
    return applied;
}

static int hexval(char c);
static bool run_init_sequence(void);

/* Le a resposta inteira de um pedido de modo 01 (ATE0/ATL0/ATS0/ATH0).
 *
 * Formatos que chegam:
 *   single frame     "410B5F2304180F43"          uma linha, uma mensagem
 *   multi-frame      "014" / "0:410B5F230418" / "1:34859880070F43" / ...
 *                    a linha de 3 hex e o tamanho em bytes; "N:" sao os
 *                    pedacos, e o ultimo vem com enchimento depois do fim
 *   varias ECUs      uma mensagem por ECU (no broadcast do perfil generico)
 *
 * Cada mensagem e aplicada sozinha. `truncated` acusa multi-frame que chegou
 * com menos bytes que o declarado — sinal de que o ELM327 parou antes da hora
 * (contagem de respostas tratada como quadros, nao mensagens). */
static uint8_t parse_mode01_response(const char *resp, bool *truncated, uint8_t *messages = NULL)
{
    uint8_t msg[64];
    size_t msg_len = 0;
    size_t declared = 0, next_declared = 0;
    bool open = false;
    uint8_t applied = 0, msgs = 0;
    *truncated = false;

    auto flush = [&]() {
        if (!open) return;
        size_t n = msg_len;
        if (declared > 0) {
            if (n < declared) *truncated = true;
            else n = declared;              /* corta o enchimento do ultimo quadro */
        }
        if (n >= 1 && msg[0] == 0x41) msgs++;
        applied += apply_mode01_message(msg, n);
        open = false;
        msg_len = 0;
        declared = 0;
    };
    auto append_hex = [&](const char *h, size_t hl) {
        for (size_t k = 0; k + 1 < hl && msg_len < sizeof(msg); k += 2)
            msg[msg_len++] = (uint8_t)((hexval(h[k]) << 4) | hexval(h[k + 1]));
    };

    const char *line = resp;
    while (*line) {
        const char *end = line;
        while (*end && *end != '\r' && *end != '\n') end++;

        char buf[96];
        size_t bl = 0;
        for (const char *c = line; c < end && bl < sizeof(buf) - 1; c++)
            if (!isspace((unsigned char)*c) && *c != '>') buf[bl++] = *c;
        buf[bl] = '\0';

        bool all_hex = bl > 0;
        for (size_t i = 0; i < bl; i++)
            if (!isxdigit((unsigned char)buf[i])) { all_hex = false; break; }

        if (bl >= 2 && isxdigit((unsigned char)buf[0]) && buf[1] == ':') {
            if (hexval(buf[0]) == 0) {           /* primeiro quadro: nova mensagem */
                flush();
                open = true;
                declared = next_declared;
                next_declared = 0;
            }
            if (open) append_hex(buf + 2, bl - 2);
        } else if (all_hex && bl == 3) {
            flush();
            next_declared = (size_t)strtoul(buf, NULL, 16);
        } else if (all_hex && bl >= 4 && (bl % 2) == 0) {
            flush();
            open = true;
            append_hex(buf, bl);
            flush();
        }
        line = (*end) ? end + 1 : end;
    }
    flush();
    if (messages) *messages = msgs;
    return applied;
}

/* Linha de diagnostico com o comando e a resposta, \r trocado por '|'. */
static void log_raw(const char *tag, const char *cmd, const char *resp)
{
    char c[24], r[96];
    size_t i = 0;
    for (; cmd[i] && cmd[i] != '\r' && i < sizeof(c) - 1; i++) c[i] = cmd[i];
    c[i] = '\0';
    size_t n = 0;
    for (const char *p = resp; *p && n < sizeof(r) - 1; p++) {
        if (*p == '>') continue;
        r[n++] = (*p == '\r' || *p == '\n') ? '|' : *p;
    }
    r[n] = '\0';
    Serial.printf("%s: %s -> %s\n", tag, c, r);
}

static void set_broadcast(void)
{
    send_cmd("ATSH7DF\r", 800);
    _physical = false;
}

/* Comeca a aprender quantas ECUs respondem em broadcast (ver _count). */
static void start_learning(void)
{
    const vehicle_profile_t *profile = vehicle_profile();
    _count = 0;
    _learn_max = 0;
    _learn_left = 0;
    if (_learned || _physical || _mode != POLL_MULTI) return;

    uint8_t max = profile->pids_per_request > OBD2_REQ_MAX ? OBD2_REQ_MAX : profile->pids_per_request;
    const uint8_t slots = (max > _n_fast) ? (uint8_t)(max - _n_fast) : 0;
    if (slots == 0) return;
    const uint8_t per_round = (uint8_t)((_n_slow + slots - 1) / slots);   /* pedidos por volta das lentas */
    _learn_left = (uint8_t)(OBD2_LEARN_ROUNDS * (per_round ? per_round : 1));
}

/* Um pedido de modo 01 com os PIDs da vez. Devolve se veio algo util. */
static bool poll_request(void)
{
    uint8_t pids[OBD2_REQ_MAX];
    const vehicle_profile_t *profile = vehicle_profile();
    uint8_t max = (_mode == POLL_SINGLE) ? 1 : profile->pids_per_request;
    if (max > OBD2_REQ_MAX) max = OBD2_REQ_MAX;
    if (max == 0) max = 1;

    const uint8_t n = (_mode == POLL_PACKED)
        ? vp_next_request_packed(_fast, _n_fast, _slow, _n_slow, OBD2_SF_BUDGET, max, &_sched, pids)
        : vp_next_request(_fast, _n_fast, _slow, _n_slow, max, &_sched, pids);
    if (n == 0) return false;

    char cmd[24];
    size_t k = (size_t)snprintf(cmd, sizeof(cmd), "01");
    for (uint8_t i = 0; i < n; i++) k += (size_t)snprintf(cmd + k, sizeof(cmd) - k, "%02X", pids[i]);
    /* Digito impar no fim = quantas respostas esperar. So com cabecalho fisico:
     * em broadcast nao se sabe quantas ECUs vao falar. */
    if (_mode == POLL_MULTI_COUNT && _count > 0) k += (size_t)snprintf(cmd + k, sizeof(cmd) - k, "%X", _count);
    snprintf(cmd + k, sizeof(cmd) - k, "\r");

    bool ok = false, truncated = false;
    uint8_t msgs = 0;
    if (send_cmd(cmd, ELM327_CMD_TIMEOUT_MS) && !response_has_error()) {
        ok = parse_mode01_response(_resp_buf, &truncated, &msgs) > 0;
    }

    if (ok) {
        vehicle.gauges.valid = true;
        notify_gauge_update();
    }

    if (ok && !truncated) {
        _bad_streak = 0;
        if (_learn_left > 0) {
            if (msgs > _learn_max) _learn_max = msgs;
            if (--_learn_left == 0) {
                _learned = true;
                if (_learn_max >= 1 && _learn_max <= 0x0F) {
                    _count = _learn_max;
                    _mode = POLL_MULTI_COUNT;
                    _sched = {};
                }
                Serial.printf("[ELM327] broadcast: %u ECU(s) respondem; %s\n", (unsigned)_learn_max,
                              _count ? "usando contagem de respostas" : "sem contagem");
            }
        }
        return ok;
    }

    if (_bad_logs < OBD2_BAD_LOG_MAX) {
        _bad_logs++;
        log_raw("[ELM327] sem leitura", cmd, _resp_buf);
    }
    if (++_bad_streak < OBD2_DEGRADE_AFTER) return ok;
    _bad_streak = 0;

    if (_mode == POLL_MULTI_COUNT) {
        _mode = POLL_MULTI;              /* sem contagem; nao reaprende (_learned) */
        _count = 0;
        _learned = true;
    } else if (_physical) {
        set_broadcast();                 /* MULTI ou SINGLE ainda na DME: tenta o 7DF */
        _mode = POLL_MULTI;
        start_learning();
    } else if (_mode == POLL_MULTI) {
        _mode = POLL_PACKED;
    } else if (_mode == POLL_PACKED) {
        _mode = POLL_SINGLE;
    } else {
        return ok;                       /* ja no ultimo degrau */
    }
    _sched = {};
    Serial.printf("[ELM327] Sem leitura util: modo %s, %s\n", POLL_MODE_NAME[_mode],
                  _physical ? "so a DME" : "broadcast");
    return ok;
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
    portENTER_CRITICAL(&_dtc_mux);
    _dtc = d;
    portEXIT_CRITICAL(&_dtc_mux);

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

/* Modo 04: apaga codigos, freeze frame e monitores de prontidao. Cada ECU
 * responde 44 (ok) ou 7F 04 NRC (recusa). Com varias ECUs basta uma aceitar. */
/* Modo 04, com uma resposta por ECU no broadcast:
 *   44         apagou;
 *   7F 04 78   "aguarde, apagando": aceito, a resposta final vem depois (e
 *              pode nem chegar dentro da janela do ELM327 — a releitura conta);
 *   7F 04 xx   recusou (0x22: motor ligado).
 * Uma recusa vence um 44 de outra ECU: no F20 quem responde 44 costuma ser o
 * cambio, e os codigos que importam ficam na DME que recusou. Dizer "apagados"
 * ai era o "apago e nao apaga". */
static void clear_dtcs(void)
{
    bool ok = false, pending = false, refused = false;
    uint8_t nrc = 0;

    if (send_cmd("04\r", DTC_CLEAR_TIMEOUT_MS) && !strstr(_resp_buf, "ERROR") && !strstr(_resp_buf, "UNABLE")) {
        char hex[128];
        size_t len = collect_hex(_resp_buf, hex, sizeof(hex), true);   /* uma resposta por linha */
        for (size_t i = 0; i < len; ) {
            size_t end = i;
            while (end < len && hex[end] != '|') end++;
            if (end - i >= 2 && hex_byte_at(hex, i) == 0x44) {
                ok = true;
            } else if (end - i >= 6 && hex_byte_at(hex, i) == 0x7F && hex_byte_at(hex, i + 2) == 0x04) {
                const uint8_t code = hex_byte_at(hex, i + 4);
                if (code == 0x78) {
                    pending = true;
                } else if (!refused) {
                    refused = true;
                    nrc = code;
                }
            }
            i = end + 1;
        }
    }

    const dtc_clear_result_t res = refused ? DTC_CLEAR_REFUSED
                                 : (ok || pending) ? DTC_CLEAR_OK
                                 : DTC_CLEAR_NO_RESPONSE;
    _dtc_clear_nrc = nrc;
    _dtc_clear_result = res;
    Serial.printf("[DTC] modo 04: %s", res == DTC_CLEAR_OK ? "codigos apagados" :
                                       res == DTC_CLEAR_REFUSED ? "recusado pela ECU" : "sem resposta");
    if (res == DTC_CLEAR_REFUSED) Serial.printf(" (NRC 0x%02X%s)", nrc, nrc == 0x22 ? ", condicoes nao atendidas: motor ligado?" : "");
    if (pending && !ok && !refused) Serial.printf(" (so 'aguarde' 0x78; a releitura confirma)");
    Serial.println();
}

const dtc_snapshot_t *elm327_dtc(void)
{
    return &_dtc;
}

void elm327_dtc_copy(dtc_snapshot_t *out)
{
    portENTER_CRITICAL(&_dtc_mux);
    *out = _dtc;
    portEXIT_CRITICAL(&_dtc_mux);
}

dtc_op_t elm327_dtc_op(void)
{
    return _dtc_op;
}

dtc_clear_result_t elm327_dtc_clear_result(uint8_t *nrc)
{
    if (nrc) *nrc = _dtc_clear_nrc;
    return _dtc_clear_result;
}

#ifdef ELM327_HOST_TEST
/* Teste no PC (preview/test_dtc.cpp): injeta respostas ELM gravadas e
 * confere o parser sem hardware. */
void elm327_test_read_dtcs(bool is_can)
{
    _is_can = is_can;
    read_dtcs();
}

void elm327_test_clear_dtcs(void)
{
    clear_dtcs();
}

/* preview/test_poll.cpp: parser multi-PID e escada de modos. */
uint8_t elm327_test_parse(const char *resp, bool *truncated)
{
    return parse_mode01_response(resp, truncated);
}

uint8_t elm327_test_parse_msgs(const char *resp)
{
    bool t = false;
    uint8_t m = 0;
    parse_mode01_response(resp, &t, &m);
    return m;
}

void elm327_test_set_plan(const uint8_t *fast, uint8_t n_fast,
                          const uint8_t *slow, uint8_t n_slow, int mode)
{
    memcpy(_fast, fast, n_fast);
    memcpy(_slow, slow, n_slow);
    _n_fast = n_fast;
    _n_slow = n_slow;
    _sched = {};
    _mode = (poll_mode_t)mode;
    _physical = (mode == POLL_MULTI_COUNT);
    _count = _physical ? 1 : 0;
    _learned = false;
    _learn_left = 0;
    _bad_streak = 0;
    _bad_logs = OBD2_BAD_LOG_MAX;     /* sem log de resposta crua nos testes */
    _baro_kpa = 0.0f;
}

bool elm327_test_poll(void) { return poll_request(); }
bool elm327_test_init(void) { return run_init_sequence(); }
bool elm327_test_physical(void) { return _physical; }
int  elm327_test_count(void) { return _count; }
int  elm327_test_mode(void) { return (int)_mode; }
#endif

/* O estado muda ja no pedido, para a UI mostrar o spinner no mesmo toque;
 * a task o devolve a IDLE quando termina. */
void elm327_request_dtc_read(void)
{
    if (_dtc_op != DTC_OP_IDLE || !_ready) return;   /* sem ELM pronto o pedido nunca seria atendido */
    _dtc_op = DTC_OP_READING;
    _dtc_request = true;
}

void elm327_request_dtc_clear(void)
{
    if (_dtc_op != DTC_OP_IDLE || !_ready) return;
    _dtc_op = DTC_OP_CLEARING;
    _dtc_clear_request = true;
}

/* ---- PIDs suportados ---------------------------------------------------- */

/* Resposta de 0100/0120/0140: "41 00 A B C D" — os quatro bytes de bitmap.
 * Em broadcast cada ECU manda o seu (DME, cambio...): junta todos, porque um
 * PID que qualquer uma publica vale a pergunta. */
static bool parse_support_bitmap(const char *resp, uint8_t pid, uint8_t out[4])
{
    const size_t len = strlen(resp);
    bool found = false;
    memset(out, 0, 4);

    for (size_t i = 0; i + 3 < len; i++) {
        if (resp[i] != '4' || resp[i + 1] != '1') continue;

        const char *p = skip_to_hex(resp + i + 2);
        uint8_t rsp_pid = 0;
        if (!parse_hex_byte(p, &rsp_pid) || rsp_pid != pid) continue;

        p += 2;
        uint8_t bytes[4] = { 0, 0, 0, 0 };
        bool ok = true;
        for (uint8_t k = 0; k < 4; k++) {
            if (!parse_hex_byte(p, &bytes[k])) { ok = false; break; }
            p = skip_to_hex(p + 2);
        }
        if (!ok) continue;

        for (uint8_t k = 0; k < 4; k++) out[k] |= bytes[k];
        found = true;
        i = (size_t)(p - resp);           /* segue para a proxima ECU */
    }
    return found;
}

/* Le 0100/0120/0140 e monta o plano de polling da sessao.
 *
 * Para quando um bloco nao responde: sem o bitmap de 0x20 nao ha como saber do
 * 0x23, e perguntar no escuro e o que se quer evitar. Se nem o primeiro bloco
 * vier, cai no plano do perfil inteiro — o 0100 ja passou no handshake, entao e
 * caso de ELM327 teimoso, nao de carro sem suporte. */
static void discover_support(void)
{
    static const uint8_t BLOCKS[] = { 0x00, 0x20, 0x40 };
    static const char *BLOCK_CMDS[] = { "0100\r", "0120\r", "0140\r" };

    _support_have = 0;
    memset(_support, 0, sizeof(_support));

    for (uint8_t i = 0; i < 3; i++) {
        if (!send_cmd(BLOCK_CMDS[i], ELM327_CMD_TIMEOUT_MS)) break;
        if (response_has_error()) break;
        if (!parse_support_bitmap(_resp_buf, BLOCKS[i], _support[i])) break;
        _support_have++;
        /* bit D0 do bloco: "ha mais PIDs adiante". Sem ele, nao segue. */
        if ((_support[i][3] & 0x01) == 0) break;
    }

    const vehicle_profile_t *profile = vehicle_profile();
    uint8_t plan[OBD2_PID_CAP];
    vp_metric_t plan_metrics[OBD2_PID_CAP];
    uint8_t n = vp_build_plan(profile, _support, _support_have, plan, plan_metrics, OBD2_PID_CAP);

    if (n == 0) {
        /* Nenhum bitmap utilizavel: usa a primeira fonte de cada grandeza. */
        for (uint8_t m = 0; m < profile->metric_count && n < OBD2_PID_CAP; m++) {
            const uint8_t pid = profile->metrics[m].pids[0];
            if (pid == 0x00) continue;
            plan_metrics[n] = profile->metrics[m].metric;
            plan[n++] = pid;
        }
    }

    /* Lambda abre a lista de rapidas. A resposta dela (41 34 A B C D) enche
     * exatamente o primeiro quadro CAN: no multi-PID, um adaptador que corta a
     * resposta no primeiro quadro ainda entrega a lambda; atras do turbo e da
     * rampa ela caia no segundo quadro e se perdia — era a lambda que "nao
     * atualizava". No agrupado, ela vai sozinha num pedido. */
    _n_fast = _n_slow = 0;
    for (uint8_t i = 0; i < n; i++)
        if (plan_metrics[i] == VP_METRIC_LAMBDA) _fast[_n_fast++] = plan[i];
    for (uint8_t i = 0; i < n; i++) {
        if (plan_metrics[i] == VP_METRIC_LAMBDA) continue;
        if (vp_metric_fast(plan_metrics[i])) _fast[_n_fast++] = plan[i];
        else                                 _slow[_n_slow++] = plan[i];
    }
    _sched = {};
    _baro_kpa = 0.0f;      /* o 0x33 entra no rodizio; ate la, nivel do mar */

    Serial.printf("[ELM327] Perfil %s: %u PIDs (%u rapidos, %u em rodizio)\n",
                  profile->name, (unsigned)n, (unsigned)_n_fast, (unsigned)_n_slow);
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
    /* Timeout adaptativo agressivo: o ELM327 mede quanto a ECU demora e para
     * de esperar logo depois disso, em vez dos ~200 ms fixos. E o que mais
     * pesa por pedido quando a contagem de respostas nao esta em uso. */
    send_cmd("ATAT2\r", 800);

    /* Protocolo do perfil quando conhecido (BMW F20 LCI: ISO 15765-4 CAN 11 bit
     * 500 kbps = ATSP6). Fixar evita a varredura do ATSP0 a cada conexao. */
    const vehicle_profile_t *profile = vehicle_profile();
    send_cmd(profile->protocol_cmd ? profile->protocol_cmd : "ATSP0\r", 1500);

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

    /* Cabecalho fisico do perfil (F20: 7E0 = so a DME). Depois do handshake,
     * que em broadcast confirma o barramento; antes dos bitmaps, que assim
     * descrevem a ECU que vai ser perguntada. */
    _physical = profile->header_cmd && _is_can &&
                send_cmd(profile->header_cmd, 800) && strstr(_resp_buf, "OK");
    if (_physical) {
        /* Prova que a DME responde no endereco direto antes de apostar nele:
         * pela tomada OBD o pedido passa pelo gateway do carro, que pode nao
         * repassar o 7E0. Sem resposta, volta ao broadcast do handshake. */
        if (!send_cmd("0100\r", 2500) || response_has_error() ||
            (!strstr(_resp_buf, "4100") && !strstr(_resp_buf, "41 00"))) {
            log_raw("[ELM327] DME nao responde em 7E0", "0100", _resp_buf);
            set_broadcast();
        }
    }

    if (profile->pids_per_request <= 1) _mode = POLL_SINGLE;
    else                                _mode = _physical ? POLL_MULTI_COUNT : POLL_MULTI;
    _count = _physical ? 1 : 0;
    _learned = false;
    _bad_streak = 0;
    _bad_logs = 0;

    discover_support();
    start_learning();                    /* depois do plano: o tamanho do aprendizado depende dele */

    Serial.printf("[ELM327] Ready, modo %s, %s\n", POLL_MODE_NAME[_mode],
                  _physical ? "so a DME (7E0)" : "broadcast (7DF)");
    return true;
}

/* Barramento para os modos 03/07/0A/04, e de volta para o polling.
 *
 * - Broadcast (7DF): com o cabecalho da DME, os codigos do cambio e das outras
 *   ECUs de emissao sumiriam da tela DTC.
 * - Timeout fixo e longo (ATAT0 + ATSTFF = ~1 s): o ATAT2 do polling aprende
 *   os ~30 ms de um modo 01 e cortaria o 04, que a DME leva centenas de ms
 *   para responder, e as ECUs mais lentas no 03.
 * Na volta, o mesmo estado do init: ATST padrao (32 = 200 ms), ATAT2 e o
 * cabecalho do perfil. */
static void dtc_bus(bool on)
{
    const vehicle_profile_t *profile = vehicle_profile();
    if (on) {
        if (_physical) send_cmd("ATSH7DF\r", 800);
        send_cmd("ATAT0\r", 800);
        send_cmd("ATSTFF\r", 800);
    } else {
        send_cmd("ATST32\r", 800);
        send_cmd("ATAT2\r", 800);
        if (_physical) send_cmd(profile->header_cmd, 800);
    }
}

static void log_rate(void)
{
    const uint32_t now = millis();
    _rate_n++;
    if (_rate_t0 == 0) _rate_t0 = now;
    if (now - _rate_t0 < OBD2_RATE_LOG_MS) return;
    const uint32_t span = now - _rate_t0;
    Serial.printf("[ELM327] %u pedidos/s (modo %s) | lambda %.1f/s, turbo %.1f/s, etanol %.1f/s\n",
                  (unsigned)(_rate_n * 1000u / span), POLL_MODE_NAME[_mode],
                  _rate_lambda * 1000.0f / span, _rate_boost * 1000.0f / span, _rate_eth * 1000.0f / span);
    _rate_t0 = now;
    _rate_n = 0;
    _rate_lambda = _rate_boost = _rate_eth = 0;
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
            Serial.printf("[ELM327] pilha: sobra minima %u B de %u\n",
                          (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)ELM327_TASK_STACK);
            continue;
        }

        if (!_ready) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* DTC so sob demanda (tela DTC): a leitura periodica custava ~2 s de
         * radio por minuto. Apagar sempre termina relendo, para a tela mostrar
         * o que sobrou (codigos permanentes nao saem com o modo 04). */
        if (_dtc_clear_request) {
            _dtc_clear_request = false;
            dtc_bus(true);
            clear_dtcs();
            _dtc_op = DTC_OP_READING;
            vTaskDelay(pdMS_TO_TICKS(DTC_CLEAR_SETTLE_MS));
            read_dtcs();
            dtc_bus(false);
            _dtc_op = DTC_OP_IDLE;
            continue;
        }
        if (_dtc_request) {
            _dtc_request = false;
            dtc_bus(true);
            read_dtcs();            /* ~10 comandos, ~2 s; o polling retoma em seguida */
            dtc_bus(false);
            _dtc_op = DTC_OP_IDLE;
            Serial.printf("[ELM327] pilha: sobra minima %u B de %u\n",
                          (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)ELM327_TASK_STACK);
            continue;
        }

        poll_request();
        log_rate();

        /* So cede a CPU: a espera de verdade e a resposta do ELM327, dentro
         * do read_response. Os 15 ms fixos de antes eram um terco do pedido. */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    _task = NULL;
    vTaskDelete(NULL);
}

static void ensure_task(void)
{
    if (_task != NULL) return;
    _task_run = true;
    /* Prioridade 2, acima do loopTask do Arduino (que e 1 e carrega LVGL e, com
     * o portal ligado, o HTTP). A task passa quase todo o tempo bloqueada em
     * vTaskDelay dentro do read_response, entao nao rouba CPU — so deixa de
     * esperar a UI para fazer a proxima troca com o ELM327. */
    /* Core 0, com a pilha do Bluetooth: o core 1 fica inteiro para o loop do
     * Arduino, que roda o LVGL — e o que segura os 60 fps da tela. */
    if (xTaskCreatePinnedToCore(elm327_task, "elm327", ELM327_TASK_STACK, NULL, 2, &_task, 0) != pdPASS) {
        _task_run = false;
        Serial.println("[ELM327] Task create failed");
    }
}

void elm327_init_session(void)
{
    _init_pending = true;
    _ready = false;
    _sched = {};
    _bad_streak = 0;
    _rate_t0 = 0;
    _rate_n = 0;
    memset(&vehicle.gauges, 0, sizeof(vehicle.gauges));
    vehicle.gauges.ethanol_pct = -1.0f;      /* 0 seria "sem etanol", nao "sem leitura" */
    ensure_task();
}

void elm327_reset_session(void)
{
    _ready = false;
    _init_pending = false;
    _dtc_request = false;          /* pedido pendente nao atravessa a desconexao */
    _dtc_clear_request = false;
    _dtc_op = DTC_OP_IDLE;
    _sched = {};
    _bad_streak = 0;
    _rate_t0 = 0;
    _rate_n = 0;
    memset(&vehicle.gauges, 0, sizeof(vehicle.gauges));
    vehicle.gauges.ethanol_pct = -1.0f;      /* 0 seria "sem etanol", nao "sem leitura" */
}

bool elm327_is_ready(void) { return _ready; }

void elm327_loop(void)
{
    /* polling runs in background task */
}
