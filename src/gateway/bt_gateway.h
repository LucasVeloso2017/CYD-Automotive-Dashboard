#ifndef BT_GATEWAY_H
#define BT_GATEWAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bt_device_data.h"

void bt_init(void);
void bt_loop(void);
void bt_start_scan(void);
void bt_stop_scan(void);
void bt_connect(uint8_t scan_index);
void bt_disconnect(void);
bool bt_is_connected(void);
bool bt_is_scanning(void);
bool bt_is_connecting(void);

int  bt_serial_available(void);
int  bt_serial_read(void);
void bt_serial_print(const char *data);

#ifdef __cplusplus
}
#endif

#endif
