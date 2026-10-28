#include "captive_dns.h"

#include <string.h>

#define DNS_HEADER_LEN   12
#define DNS_ANSWER_A_LEN 16

int captive_dns_answer(uint8_t *buf, int len, int cap, uint32_t ip_net)
{
    if (!buf || len < DNS_HEADER_LEN) return -1;
    if (buf[2] & 0x80) return -1;                  /* e resposta, nao pergunta */
    if (buf[4] != 0 || buf[5] != 1) return -1;     /* so uma pergunta por pacote */

    /* Pula o nome: rotulos com o tamanho na frente, terminados em zero. Ponteiro
     * de compressao nao aparece em pergunta; se aparecer, descarta. */
    int p = DNS_HEADER_LEN;
    while (p < len && buf[p] != 0) {
        if (buf[p] & 0xC0) return -1;
        p += buf[p] + 1;
    }
    p += 1;                                        /* o zero final */
    if (p + 4 > len) return -1;                    /* sem QTYPE/QCLASS */

    const int type_a = buf[p] == 0x00 && buf[p + 1] == 0x01;
    int out = p + 4;                               /* a pergunta volta intacta */
    if (type_a && out + DNS_ANSWER_A_LEN > cap) return -1;

    buf[2] = (uint8_t)(0x80 | (buf[2] & 0x79) | 0x04);   /* QR, opcode e RD copiados, AA */
    buf[3] = 0x80;                                       /* RA, RCODE 0 */
    buf[6] = 0x00;
    buf[7] = (uint8_t)(type_a ? 1 : 0);                  /* ANCOUNT */
    buf[8] = buf[9] = buf[10] = buf[11] = 0;             /* sem NS nem adicional */

    if (type_a) {
        static const uint8_t HEAD[] = {
            0xC0, 0x0C,             /* nome: ponteiro para a pergunta */
            0x00, 0x01, 0x00, 0x01, /* tipo A, classe IN */
            0x00, 0x00, 0x00, 0x3C, /* TTL 60 s */
            0x00, 0x04,             /* 4 bytes de endereco */
        };
        memcpy(buf + out, HEAD, sizeof(HEAD));
        out += (int)sizeof(HEAD);
        memcpy(buf + out, &ip_net, 4);
        out += 4;
    }
    return out;
}
