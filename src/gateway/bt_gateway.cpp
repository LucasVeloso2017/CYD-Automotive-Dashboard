#include "bt_gateway.h"
#include "elm327_gateway.h"

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

extern "C" {
    bt_device_state_t bt_state = {};
}

static BluetoothSerial SerialBT;
static bool _initialized = false;
static bool _scanning = false;
static bool _connecting = false;
static int8_t _pending_connect = -1;
static TaskHandle_t _connect_task = NULL;
static unsigned long _scan_started_at = 0;

#define BT_SCAN_TIMEOUT_MS  12800

static void perform_connect(uint8_t scan_index);

static void connect_task(void *param)
{
    uint8_t idx = (uint8_t)(uintptr_t)param;
    perform_connect(idx);
    _connect_task = NULL;
    vTaskDelete(NULL);
}

static void bt_scan_callback(BTAdvertisedDevice *dev)
{
    if (!dev || bt_state.count >= BT_MAX_SCAN_RESULTS) return;

    bt_scan_result_t *r = &bt_state.results[bt_state.count];

    std::string devName = dev->getName();
    if (devName.length() > 0) {
        strncpy(r->name, devName.c_str(), BT_DEVICE_NAME_LEN - 1);
        r->name[BT_DEVICE_NAME_LEN - 1] = '\0';
    } else {
        snprintf(r->name, BT_DEVICE_NAME_LEN, "Device %d", bt_state.count + 1);
    }

    String addrStr = dev->getAddress().toString();
    strncpy(r->addr, addrStr.c_str(), BT_ADDR_LEN - 1);
    r->addr[BT_ADDR_LEN - 1] = '\0';
    r->rssi = dev->getRSSI();

    bt_state.count++;
    Serial.printf("[BT] Found: %s (%s) %ddBm\n", r->name, r->addr, r->rssi);
}

static bool connect_to_device(const bt_scan_result_t *r)
{
    uint8_t addr[6];
    if (sscanf(r->addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) != 6) {
        Serial.println("[BT] Invalid address format");
        return false;
    }

    bool ok = SerialBT.connect(addr);
    if (!ok && r->name[0] != '\0') {
        Serial.printf("[BT] MAC connect failed, trying by name: %s\n", r->name);
        ok = SerialBT.connect(r->name);
    }
    return ok;
}

static void perform_connect(uint8_t scan_index)
{
    if (scan_index >= bt_state.count) return;

    _connecting = true;
    bt_state.connecting = true;

    bt_stop_scan();
    delay(300);

    if (bt_state.connected) {
        SerialBT.disconnect();
        bt_state.connected = false;
        bt_state.connected_index = -1;
        bt_state.connected_name[0] = '\0';
        bt_state.connected_addr[0] = '\0';
        delay(200);
    }

    bt_scan_result_t *r = &bt_state.results[scan_index];
    Serial.printf("[BT] Connecting to %s (%s)\n", r->name, r->addr);

    if (connect_to_device(r)) {
        bt_state.connected = true;
        bt_state.connected_index = (int8_t)scan_index;
        strncpy(bt_state.connected_name, r->name, BT_DEVICE_NAME_LEN - 1);
        bt_state.connected_name[BT_DEVICE_NAME_LEN - 1] = '\0';
        strncpy(bt_state.connected_addr, r->addr, BT_ADDR_LEN - 1);
        bt_state.connected_addr[BT_ADDR_LEN - 1] = '\0';
        Serial.printf("[BT] Connected to %s\n", r->name);
        elm327_init_session();
    } else {
        bt_state.connected = false;
        bt_state.connected_index = -1;
        Serial.println("[BT] Connection failed");
    }

    _connecting = false;
    bt_state.connecting = false;
}

void bt_init(void)
{
    if (_initialized) return;

    if (!SerialBT.begin("CarDash", true)) {
        Serial.println("[BT] Init failed");
        return;
    }

    SerialBT.setPin("1234", 4);
    _initialized = true;
    Serial.println("[BT] Ready (Classic SPP master)");
}

void bt_start_scan(void)
{
    if (!_initialized || _connecting || bt_state.connected) return;
    if (_scanning || bt_state.scanning) return;

    bt_state.count = 0;
    bt_state.scanning = true;
    _scanning = true;
    memset(bt_state.results, 0, sizeof(bt_state.results));

    Serial.println("[BT] Scanning...");
    _scan_started_at = millis();
    if (!SerialBT.discoverAsync(bt_scan_callback, BT_SCAN_TIMEOUT_MS)) {
        Serial.println("[BT] Scan start failed");
        _scanning = false;
        bt_state.scanning = false;
    }
}

void bt_stop_scan(void)
{
    if (_scanning || bt_state.scanning) {
        SerialBT.discoverAsyncStop();
        _scanning = false;
        bt_state.scanning = false;
        Serial.printf("[BT] Scan done, %d device(s)\n", bt_state.count);
    }
}

void bt_connect(uint8_t scan_index)
{
    if (scan_index >= bt_state.count || _connecting || _connect_task) return;
    if (bt_state.connected &&
        strncmp(bt_state.connected_addr, bt_state.results[scan_index].addr, BT_ADDR_LEN) == 0) {
        return;
    }
    _pending_connect = (int8_t)scan_index;
    bt_state.connecting = true;
}

void bt_disconnect(void)
{
    if (bt_state.connected) {
        SerialBT.disconnect();
    }
    elm327_reset_session();
    bt_state.connected = false;
    bt_state.connected_index = -1;
    bt_state.connected_name[0] = '\0';
    bt_state.connected_addr[0] = '\0';
}

bool bt_is_connected(void) { return bt_state.connected; }
bool bt_is_scanning(void) { return bt_state.scanning; }
bool bt_is_connecting(void) { return bt_state.connecting; }

int bt_serial_available(void)
{
    return SerialBT.available();
}

int bt_serial_read(void)
{
    return SerialBT.read();
}

void bt_serial_print(const char *data)
{
    if (data) SerialBT.print(data);
}

void bt_loop(void)
{
    if (!_initialized) return;

    if (_scanning && (millis() - _scan_started_at >= BT_SCAN_TIMEOUT_MS)) {
        bt_stop_scan();
    }

    if (bt_state.connected && !SerialBT.connected()) {
        Serial.println("[BT] Connection lost");
        elm327_reset_session();
        bt_state.connected = false;
        bt_state.connected_index = -1;
        bt_state.connected_name[0] = '\0';
        bt_state.connected_addr[0] = '\0';
    } else if (!bt_state.connected && SerialBT.connected()) {
        bt_state.connected = true;
        elm327_init_session();
    }

    elm327_loop();

    if (_pending_connect >= 0 && !_connecting && _connect_task == NULL) {
        uint8_t idx = (uint8_t)_pending_connect;
        _pending_connect = -1;
        if (xTaskCreate(connect_task, "bt_conn", 8192, (void *)(uintptr_t)idx, 1, &_connect_task) != pdPASS) {
            bt_state.connecting = false;
            Serial.println("[BT] Failed to start connect task");
        }
    }
}
