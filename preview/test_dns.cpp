// Teste do DNS do portal cativo no PC, sem hardware.
//
//   cd preview && make -f test_dns.mk
//
// Sem argumento: verifica a montagem da resposta byte a byte, inclusive os
// pacotes que tem de ser ignorados. Com "serve <porta>": sobe um servidor UDP
// em 127.0.0.1 usando a mesma funcao do firmware, para conferir com um
// resolvedor de verdade:  dig @127.0.0.1 -p <porta> qualquer.coisa

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "captive_dns.h"

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("  FALHOU: " __VA_ARGS__); printf("\n"); } } while (0)

static const uint32_t AP_IP = 0x0104A8C0u;   /* 192.168.4.1 em ordem de rede (little-endian no host) */

/* Pergunta para "dash.local" tipo `qtype`, id 0xBEEF, RD ligado. */
static int query(uint8_t *b, uint16_t qtype)
{
    const uint8_t q[] = {
        0xBE, 0xEF, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        4, 'd', 'a', 's', 'h', 5, 'l', 'o', 'c', 'a', 'l', 0,
        (uint8_t)(qtype >> 8), (uint8_t)qtype, 0x00, 0x01,
    };
    memcpy(b, q, sizeof(q));
    return (int)sizeof(q);
}

static int serve(int port)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (sockaddr *)&a, sizeof(a)) < 0) { perror("bind"); return 1; }
    printf("ouvindo em 127.0.0.1:%d\n", port);
    fflush(stdout);
    for (;;) {
        uint8_t buf[300];
        sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = (int)recvfrom(s, buf, sizeof(buf) - 16, 0, (sockaddr *)&from, &fl);
        int len = captive_dns_answer(buf, n, (int)sizeof(buf), AP_IP);
        if (len > 0) sendto(s, buf, (size_t)len, 0, (sockaddr *)&from, fl);
    }
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "serve") == 0) return serve(atoi(argv[2]));

    uint8_t b[300];

    printf("== pergunta A vira resposta com o IP do AP ==\n");
    int n = query(b, 1);
    int r = captive_dns_answer(b, n, (int)sizeof(b), AP_IP);
    CHECK(r == n + 16, "resposta A devia ter %d bytes, veio %d", n + 16, r);
    CHECK(b[0] == 0xBE && b[1] == 0xEF, "o id da pergunta tem que voltar igual");
    CHECK((b[2] & 0x80) && (b[2] & 0x04) && (b[2] & 0x01), "QR, AA e RD copiado: flags 0x%02X", b[2]);
    CHECK(b[3] == 0x80, "RA ligado e RCODE 0, veio 0x%02X", b[3]);
    CHECK(b[7] == 1, "ANCOUNT devia ser 1, veio %u", b[7]);
    CHECK(memcmp(b + r - 4, &AP_IP, 4) == 0, "os 4 ultimos bytes tem que ser o IP do AP");
    CHECK(b[n] == 0xC0 && b[n + 1] == 0x0C, "o nome da resposta aponta para a pergunta");

    printf("== AAAA: resposta valida e vazia, sem esperar timeout ==\n");
    n = query(b, 28);
    r = captive_dns_answer(b, n, (int)sizeof(b), AP_IP);
    CHECK(r == n, "AAAA devolve so a pergunta, %d bytes, veio %d", n, r);
    CHECK(b[7] == 0 && (b[2] & 0x80), "AAAA: resposta sem registro");

    printf("== pacotes ignorados ==\n");
    CHECK(captive_dns_answer(b, 5, (int)sizeof(b), AP_IP) == -1, "menor que o cabecalho");
    n = query(b, 1); b[2] |= 0x80;
    CHECK(captive_dns_answer(b, n, (int)sizeof(b), AP_IP) == -1, "resposta nao e respondida");
    n = query(b, 1); b[5] = 2;
    CHECK(captive_dns_answer(b, n, (int)sizeof(b), AP_IP) == -1, "mais de uma pergunta");
    n = query(b, 1); b[12] = 0xC0;
    CHECK(captive_dns_answer(b, n, (int)sizeof(b), AP_IP) == -1, "nome comprimido na pergunta");
    n = query(b, 1);
    CHECK(captive_dns_answer(b, n - 3, (int)sizeof(b), AP_IP) == -1, "QTYPE/QCLASS cortados");
    n = query(b, 1); b[12] = 60;    /* rotulo maior que o pacote */
    CHECK(captive_dns_answer(b, n, (int)sizeof(b), AP_IP) == -1, "rotulo que estoura o pacote");
    n = query(b, 1);
    CHECK(captive_dns_answer(b, n, n + 15, AP_IP) == -1, "sem espaco para a resposta A");
    CHECK(captive_dns_answer(NULL, 40, 300, AP_IP) == -1, "buffer nulo");

    printf(g_fail ? "\n%d verificacao(oes) falharam\n" : "\nok\n", g_fail);
    return g_fail ? 1 : 0;
}
