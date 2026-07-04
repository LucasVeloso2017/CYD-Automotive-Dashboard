#ifndef BT_DEVICE_DATA_H
#define BT_DEVICE_DATA_H

#include <stdbool.h>
#include <stdint.h>

#define BT_DEVICE_NAME_LEN   48
#define BT_ADDR_LEN          18
#define BT_MAX_SCAN_RESULTS  6

typedef struct {
    char name[BT_DEVICE_NAME_LEN];
    char addr[BT_ADDR_LEN];
    int  rssi;
} bt_scan_result_t;

typedef struct {
    bool scanning;
    bool connecting;
    bool connected;
    char connected_name[BT_DEVICE_NAME_LEN];
    char connected_addr[BT_ADDR_LEN];
    int8_t connected_index;

    bt_scan_result_t results[BT_MAX_SCAN_RESULTS];
    uint8_t count;
} bt_device_state_t;

#ifdef __cplusplus
extern "C" {
#endif

extern bt_device_state_t bt_state;

#ifdef __cplusplus
}
#endif

#endif
