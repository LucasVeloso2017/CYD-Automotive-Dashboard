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

/* Conexao automatica no boot: enquanto a janela estiver aberta, o primeiro
 * dispositivo da busca com nome de dongle OBD2 ("OBDII", "OBD II", "OBD2"...)
 * e conectado sozinho. Fechada a janela sem achar, nada acontece: o usuario
 * escolhe na lista Bluetooth. Liga a busca se ela nao estiver rodando. */
void bt_autoconnect_start(uint32_t window_ms);
/** A janela ainda esta aberta (buscando)? */
bool bt_autoconnect_active(void);
/** Achou um dongle na janela (a conexao pode ainda estar em andamento). */
bool bt_autoconnect_found(void);

int  bt_serial_available(void);
int  bt_serial_read(void);
void bt_serial_print(const char *data);

#ifdef __cplusplus
}
#endif

#endif
