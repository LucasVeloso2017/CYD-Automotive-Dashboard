#ifndef CAPTIVE_DNS_H
#define CAPTIVE_DNS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Monta, no proprio buffer da pergunta, a resposta do DNS do portal cativo.
 *
 * Toda pergunta tipo A e respondida com o IP do AP; os outros tipos (AAAA,
 * HTTPS...) recebem resposta valida e vazia, para o celular nao ficar
 * esperando timeout. Funcao pura, sem socket, para ser testada no PC.
 *
 * @param buf     Pergunta recebida; a resposta e escrita por cima.
 * @param len     Bytes validos da pergunta.
 * @param cap     Capacidade de `buf` (a resposta A acrescenta 16 bytes).
 * @param ip_net  IPv4 do AP em ordem de rede (como o lwIP guarda).
 * @return Tamanho da resposta, ou -1 quando o pacote deve ser ignorado
 *         (truncado, e resposta, mais de uma pergunta, nome comprimido...).
 */
int captive_dns_answer(uint8_t *buf, int len, int cap, uint32_t ip_net);

#ifdef __cplusplus
}
#endif

#endif
