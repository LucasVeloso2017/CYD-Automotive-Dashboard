#ifndef LINK_PROTO_H
#define LINK_PROTO_H

/* Protocolo serial entre o CYD e o Wemos D1 mini.
 *
 * O CYD fica com o Bluetooth (ELM327) e os gauges; o Wemos, que nao tem
 * Bluetooth, fica com o WiFi e a pagina web. No CYD os dois radios nao cabiam
 * juntos na memoria nem no tempo de antena; separados, cada chip tem o seu.
 *
 * Fio: UART 3,3 V, LINK_BAUD 8N1.
 *   CYD GPIO 27 (TX) -> Wemos D7/GPIO13 (RX)
 *   CYD GPIO 22 (RX) <- Wemos D8/GPIO15 (TX)
 *   GND comum. Wemos alimentado pelo 5 V, nao pelo 3,3 V do CYD.
 *
 * Quadro: uma linha de texto
 *     <tipo><conteudo>*<CC>\n
 * CC = XOR de todos os bytes de tipo+conteudo, em dois hex maiusculos. Linha
 * com CC errado e descartada inteira: um byte trocado no fio vira uma
 * atualizacao perdida, nunca um numero errado na tela do celular.
 *
 * CYD -> Wemos
 *   S  estado (JSON igual ao de /api/state), a cada LINK_STATE_MS
 *   G  amostra para o historico (inteiros em escala fixa), junto com o S
 *   D  lista de DTC (JSON igual ao de /api/dtc), quando muda e a cada LINK_DTC_REFRESH_MS
 * Wemos -> CYD
 *   W  rede e celulares: ssid TAB senha TAB url TAB aparelhos, a cada LINK_HELLO_MS
 *   R  ler DTC
 *   X  apagar DTC
 *
 * ESTE ARQUIVO EXISTE EM DOIS LUGARES, IDENTICOS: src/net/link_proto.h (CYD)
 * e wemos-portal/src/link_proto.h (Wemos). Arduino nao compila arquivo de
 * fora da pasta do sketch, entao nao da para incluir um so. O teste
 * preview/test_link confere que as duas copias sao byte a byte iguais.
 *
 * So funcoes static inline e C puro: compila no ESP32, no ESP8266 e no PC.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINK_BAUD          115200
#define LINK_MAX_LINE      5120    /* maior quadro: D com 36 codigos descritos */
#define LINK_STATE_MS      250     /* S + G: ~1,7 kB/s, 15% do fio a 115200 */
#define LINK_HELLO_MS      2000    /* W */
#define LINK_STALE_MS      3000    /* sem S/W ha mais que isto: o outro lado sumiu */
#define LINK_DTC_REFRESH_MS 15000  /* D reenviado mesmo sem mudanca: um D perdido no fio nao fica perdido */

#define LINK_STATE   'S'
#define LINK_SAMPLE  'G'
#define LINK_DTC     'D'
#define LINK_HELLO   'W'
#define LINK_READ    'R'
#define LINK_CLEAR   'X'

static inline uint8_t link_xor(const char *s, size_t n)
{
    uint8_t x = 0;
    while (n--) x ^= (uint8_t)*s++;
    return x;
}

/* So maiusculo, que e o que link_seal escreve: aceitar 'a' como 'A' deixaria
 * passar um bit trocado no proprio checksum. */
static inline int link_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Fecha um quadro: acrescenta "*CC\n" ao conteudo em buf[0..len).
 * @return novo tamanho, ou -1 se nao couber em cap.
 */
static inline int link_seal(char *buf, size_t len, size_t cap)
{
    if (!buf || len == 0 || len + 5 > cap) return -1;
    static const char DIGITS[] = "0123456789ABCDEF";   /* nao "HEX": macro do Arduino */
    const uint8_t x = link_xor(buf, len);
    buf[len++] = '*';
    buf[len++] = DIGITS[x >> 4];
    buf[len++] = DIGITS[x & 0x0F];
    buf[len++] = '\n';
    buf[len] = '\0';
    return (int)len;
}

/**
 * Confere uma linha recebida (com ou sem '\n'/'\r' no fim) e corta o "*CC".
 * @return tamanho de tipo+conteudo, ja terminado em '\0'; -1 se invalida.
 */
static inline int link_verify(char *line, size_t len)
{
    if (!line) return -1;
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    if (len < 4 || line[len - 3] != '*') return -1;
    const int hi = link_hexval(line[len - 2]);
    const int lo = link_hexval(line[len - 1]);
    if (hi < 0 || lo < 0) return -1;
    const size_t body = len - 3;
    if (link_xor(line, body) != (uint8_t)((hi << 4) | lo)) return -1;
    line[body] = '\0';
    return (int)body;
}

/* ---- G: amostra para o historico ----------------------------------------
 *
 * Inteiros em escala fixa, na mesma ordem e escala do historico do Wemos:
 * texto curto, sem float no fio (o sscanf de float do ESP8266 e opcional). Um
 * "-" no lugar do numero e grandeza sem leitura (NaN). */

enum {
    LINK_F_LAMBDA = 0,
    LINK_F_WATER,
    LINK_F_BATT,
    LINK_F_BOOST,
    LINK_F_COMB,
    LINK_F_IAT,
    LINK_F_OIL,
    LINK_F_COUNT
};

static const float LINK_SCALE[LINK_F_COUNT] = {
    1000.0f, 10.0f, 100.0f, 1000.0f, 10.0f, 10.0f, 10.0f,
};

typedef struct {
    int   valid;                 /* ha sessao com o ELM327 e leitura */
    float v[LINK_F_COUNT];       /* NaN = sem leitura */
} link_sample_t;

/** Escreve "G<valid>,<v0>,...,<v6>" em out. @return tamanho, ou -1. */
static inline int link_sample_format(char *out, size_t cap, const link_sample_t *s)
{
    if (!out || !s) return -1;
    int n = snprintf(out, cap, "%c%d", LINK_SAMPLE, s->valid ? 1 : 0);
    for (int i = 0; i < LINK_F_COUNT && n > 0 && (size_t)n < cap; i++) {
        const float v = s->v[i];
        if (!isfinite(v)) {
            n += snprintf(out + n, cap - (size_t)n, ",-");
        } else {
            float k = v * LINK_SCALE[i];
            if (k > 2147483000.0f) k = 2147483000.0f;
            if (k < -2147483000.0f) k = -2147483000.0f;
            n += snprintf(out + n, cap - (size_t)n, ",%ld", (long)lroundf(k));
        }
    }
    return (n > 0 && (size_t)n < cap) ? n : -1;
}

/** Le o conteudo de um quadro G (com o 'G' na frente). @return 0 ou -1. */
static inline int link_sample_parse(const char *p, link_sample_t *s)
{
    if (!p || !s || p[0] != LINK_SAMPLE) return -1;
    p++;
    char *end;
    const long valid = strtol(p, &end, 10);
    if (end == p) return -1;
    s->valid = valid != 0;
    p = end;
    for (int i = 0; i < LINK_F_COUNT; i++) {
        if (*p != ',') return -1;
        p++;
        if (*p == '-' && (p[1] == ',' || p[1] == '\0')) {
            s->v[i] = NAN;
            p++;
            continue;
        }
        const long k = strtol(p, &end, 10);
        if (end == p) return -1;
        s->v[i] = (float)k / LINK_SCALE[i];
        p = end;
    }
    return *p == '\0' ? 0 : -1;
}

/* ---- W: rede do Wemos ---------------------------------------------------- */

#define LINK_SSID_MAX  33
#define LINK_PASS_MAX  65
#define LINK_URL_MAX   40

typedef struct {
    char    ssid[LINK_SSID_MAX];
    char    pass[LINK_PASS_MAX];
    char    url[LINK_URL_MAX];
    uint8_t clients;
} link_hello_t;

static inline int link_hello_format(char *out, size_t cap, const link_hello_t *h)
{
    if (!out || !h) return -1;
    const int n = snprintf(out, cap, "%c%s\t%s\t%s\t%u", LINK_HELLO,
                           h->ssid, h->pass, h->url, (unsigned)h->clients);
    return (n > 0 && (size_t)n < cap) ? n : -1;
}

/* Copia o campo ate o proximo TAB (ou fim). @return ponteiro depois do TAB, ou NULL. */
static inline const char *link_field(const char *p, char *dst, size_t cap, int last)
{
    size_t i = 0;
    while (*p && *p != '\t') {
        if (i + 1 >= cap) return NULL;     /* campo maior que o destino */
        dst[i++] = *p++;
    }
    dst[i] = '\0';
    if (last) return *p == '\0' ? p : NULL;
    return *p == '\t' ? p + 1 : NULL;
}

static inline int link_hello_parse(const char *p, link_hello_t *h)
{
    if (!p || !h || p[0] != LINK_HELLO) return -1;
    char num[6];
    p = link_field(p + 1, h->ssid, sizeof(h->ssid), 0);
    if (p) p = link_field(p, h->pass, sizeof(h->pass), 0);
    if (p) p = link_field(p, h->url, sizeof(h->url), 0);
    if (p) p = link_field(p, num, sizeof(num), 1);
    if (!p || !num[0]) return -1;
    char *end;
    const long c = strtol(num, &end, 10);
    if (*end || c < 0 || c > 255) return -1;
    h->clients = (uint8_t)c;
    return 0;
}

#endif
