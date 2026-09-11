#ifndef TRACK_SERVER_H
#define TRACK_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Access point do ESP32 para a pagina de track-metrics. O celular entra nesta
 * rede (via QR code na tela ui_wifi) e o captive portal abre a pagina sozinho.
 * Senha WPA2 precisa de 8+ caracteres. */
#define TRACK_AP_SSID   "CarDash"
#define TRACK_AP_PASS   "trackday1"
#define TRACK_AP_URL    "http://192.168.4.1"

/* Payload do QR no formato que a camera do iOS/Android reconhece e oferece
 * "conectar a rede". Nao codifica a URL: o captive portal cuida disso. */
#define TRACK_AP_QR     "WIFI:T:WPA;S:" TRACK_AP_SSID ";P:" TRACK_AP_PASS ";;"

/* A UI pede o start; quem sobe o AP de fato e track_server_loop(), no topo
 * da loopTask. Inicializar Wi-Fi de dentro de um callback do LVGL (pilha ja
 * funda) arrisca estourar a stack da loopTask e resetar o ESP. */
void    track_server_request_start(void);
void    track_server_request_stop(void);      /* ao sair da tela: derruba o AP e retoma o BT */
void    track_server_start(void);     /* sincrono; idempotente */
void    track_server_stop(void);
void    track_server_loop(void);      /* chamar a cada volta do loop() */
bool    track_server_running(void);
bool    track_server_failed(void);    /* start tentou e nao conseguiu (ver serial) */
uint8_t track_server_clients(void);   /* celulares associados ao AP */

#ifdef __cplusplus
}
#endif

#endif
