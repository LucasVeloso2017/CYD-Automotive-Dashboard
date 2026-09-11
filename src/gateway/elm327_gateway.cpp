#include "elm327_gateway.h"

#include "bt_gateway.h"
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

static char _resp_buf[160];
static size_t _resp_len = 0;

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
