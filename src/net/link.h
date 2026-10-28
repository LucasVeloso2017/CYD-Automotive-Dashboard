#ifndef LINK_H
#define LINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ponte serial para o Wemos D1 mini, que serve a pagina web (wemos-portal/).
 *
 * O CYD nao tem memoria nem tempo de antena para Bluetooth e WiFi juntos (medido:
 * com o portal no ar sobravam 24 kB e o celular conectava de forma intermitente).
 * Entao o radio WiFi foi para outro chip: o CYD manda o estado pelo fio a cada
 * 250 ms e atende os pedidos de DTC que chegam da pagina. Protocolo em
 * link_proto.h; tudo aqui roda na loop(). */

void link_init(void);
void link_loop(void);

/** O Wemos deu sinal de vida nos ultimos LINK_STALE_MS? */
bool link_portal_alive(void);

/** Rede do Wemos, como ele anunciou (vazio enquanto nao anunciou). */
const char *link_portal_ssid(void);
const char *link_portal_password(void);
const char *link_portal_url(void);
uint8_t     link_portal_clients(void);

/** Conteudo do QR: "WIFI:T:WPA;S:...;P:...;;", que a camera do celular entende. */
const char *link_portal_qr_payload(void);

#ifdef __cplusplus
}
#endif

#endif
