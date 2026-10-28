/*
 * Wemos D1 mini (ESP8266) - portal web do Car Dashboard
 *
 * O CYD (ESP32) fica com o Bluetooth do ELM327 e os gauges; este Wemos, que nem
 * tem Bluetooth, fica com o WiFi: cria a rede, serve a pagina com o resumo dos
 * gauges, o grafico do historico e o apagamento de DTC. Os dados chegam do CYD
 * por um fio serial; os pedidos da pagina voltam pelo mesmo fio. Protocolo em
 * src/link_proto.h (copia identica de ../src/net/link_proto.h).
 *
 * Ligacao (3,3 V nas duas pontas, sem conversor):
 *   CYD CN1 GPIO 27 (TX) -> D7 / GPIO13 (RX)
 *   CYD CN1 GPIO 22 (RX) <- D8 / GPIO15 (TX)
 *   CYD CN1 GND          -- GND
 *   Alimentacao: 5 V no pino 5V do Wemos (ou USB). Nao use o 3,3 V do CYD: os
 *   picos de 300-400 mA do WiFi derrubariam o regulador dele.
 *
 * Arduino IDE / arduino-cli:
 *   Placa:  LOLIN(WEMOS) D1 R2 & mini   (esp8266:esp8266:d1_mini)
 *   O USB continua servindo para gravar: o bootloader usa os pinos padrao, e
 *   so depois do boot a serial e remapeada para D7/D8 (Serial.swap()).
 *   Log de depuracao sai em D4/GPIO2 (Serial1, so TX, 115200) - opcional.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>

#include "src/captive_dns.h"
#include "src/history.h"
#include "src/link_proto.h"
#include "src/web_page.h"

#define AP_CHANNEL       1
#define AP_MAX_CLIENTS   4
#define DNS_PORT         53
#define RX_BUFFER        6144     /* UART: um quadro D inteiro com folga */
#define STATE_MAX        1024     /* o S tem ~500 B */
#define DEBUG_LOG_MS     10000

static ESP8266WebServer server(80);
static WiFiUDP dns;
static const IPAddress ap_ip(192, 168, 4, 1);

static link_hello_t hello;          /* rede deste Wemos, anunciada ao CYD */

/* Recepcao em dois buffers do tamanho do maior quadro. Quando chega um D
 * valido, os ponteiros trocam de papel: o quadro vira a lista servida e o outro
 * buffer passa a receber. Nada de copiar 5 kB a cada lista nova. */
static char  buf_a[LINK_MAX_LINE];
static char  buf_b[LINK_MAX_LINE];
static char *rx = buf_a;
static char *dtc = buf_b;           /* "D{...}" do ultimo quadro D valido */
static size_t rx_len = 0;
static bool  rx_overflow = false;
static bool  have_dtc = false;

static char     state[STATE_MAX];   /* "{...}" do ultimo quadro S valido */
static bool     have_state = false;
static uint32_t state_ms = 0;

static bool     session_valid = false;
static uint32_t frames_ok = 0, frames_bad = 0;
static uint32_t last_hello_ms = 0, last_debug_ms = 0;

/* ---- fio com o CYD ------------------------------------------------------- */

static void send_frame(char type)
{
    char b[8] = { type, '\0' };
    if (link_seal(b, 1, sizeof(b)) > 0) Serial.print(b);
}

static void send_hello(void)
{
    hello.clients = (uint8_t)WiFi.softAPgetStationNum();
    char b[160];
    const int n = link_hello_format(b, sizeof(b), &hello);
    if (n > 0 && link_seal(b, (size_t)n, sizeof(b)) > 0) Serial.print(b);
}

static void on_sample(const char *line)
{
    link_sample_t s;
    if (link_sample_parse(line, &s) != 0) return;

    /* Sessao nova no CYD (o ELM327 conectou): historico do zero. Quando a
     * sessao cai, o grafico fica como estava - estacionado, e justamente ai
     * que se quer olhar o que aconteceu. */
    if (s.valid && !session_valid) history_reset();
    session_valid = s.valid;
    if (!s.valid) return;

    history_sample_t h;
    h.lambda            = s.v[LINK_F_LAMBDA];
    h.water_temp_c      = s.v[LINK_F_WATER];
    h.battery_v         = s.v[LINK_F_BATT];
    h.boost_bar         = s.v[LINK_F_BOOST];
    h.comb_pressure_bar = s.v[LINK_F_COMB];
    h.iat_c             = s.v[LINK_F_IAT];
    h.oil_temp_c        = s.v[LINK_F_OIL];
    history_tick(&h, millis());
}

static void on_line(void)
{
    const int body = link_verify(rx, rx_len);
    if (body <= 0) {
        frames_bad++;
        return;
    }
    frames_ok++;

    switch (rx[0]) {
    case LINK_STATE:
        if ((size_t)body < sizeof(state)) {          /* body inclui o 'S' */
            memcpy(state, rx + 1, (size_t)body);      /* copia o '\0' tambem */
            have_state = true;
            state_ms = millis();
        }
        break;
    case LINK_SAMPLE:
        on_sample(rx);
        break;
    case LINK_DTC: {
        char *t = dtc;
        dtc = rx;
        rx = t;
        have_dtc = true;
        break;
    }
    default:
        break;
    }
}

static void receive(void)
{
    while (Serial.available() > 0) {
        const char c = (char)Serial.read();
        if (c == '\n') {
            if (!rx_overflow && rx_len > 0) {
                rx[rx_len] = '\0';
                on_line();
            } else if (rx_overflow) {
                frames_bad++;
            }
            rx_len = 0;
            rx_overflow = false;
        } else if (rx_len + 1 < LINK_MAX_LINE) {
            rx[rx_len++] = c;
        } else {
            rx_overflow = true;        /* descarta ate o proximo '\n' */
        }
    }
}

static bool cyd_alive(void)
{
    return have_state && (uint32_t)(millis() - state_ms) < LINK_STALE_MS;
}

/* ---- pagina e API ----------------------------------------------------------- */

static void handle_root(void)
{
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", WEB_PAGE_HTML);
}

/* O JSON do estado e o que o CYD mandou, tal e qual. Sem quadro recente, uma
 * resposta com "cyd":false: a pagina mostra "painel sem resposta" em vez de
 * numeros velhos como se fossem atuais. */
static void handle_state(void)
{
    server.sendHeader("Cache-Control", "no-store");
    if (cyd_alive()) {
        server.send(200, "application/json", state);
        return;
    }
    server.send(200, "application/json",
        "{\"cyd\":false,\"bt\":false,\"link\":false,\"valid\":false,\"profile\":\"\","
        "\"g\":{},\"max\":{},\"dtc\":{\"valid\":false,\"mil\":false,\"op\":0,\"stored\":0,"
        "\"pending\":0,\"permanent\":0,\"read_ms\":0,\"clear\":0,\"nrc\":0}}");
}

static void handle_dtc_list(void)
{
    server.sendHeader("Cache-Control", "no-store");
    if (have_dtc) server.send(200, "application/json", dtc + 1);
    else          server.send(200, "application/json", "{\"codes\":[],\"ff\":null,\"read_ms\":0}");
}

static void json_num(char *out, size_t cap, size_t *len, float v, int dec)
{
    const int n = isfinite(v) ? snprintf(out + *len, cap - *len, "%.*f", dec, (double)v)
                              : snprintf(out + *len, cap - *len, "null");
    if (n > 0) *len += (size_t)n;
}

/* Historico em chunks: 300 amostras x 7 series nao cabem num buffer so. */
static void handle_history(void)
{
    const uint16_t n = history_count();
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    char head[80];
    snprintf(head, sizeof(head), "{\"interval_ms\":%d,\"n\":%u,\"series\":{",
             HISTORY_INTERVAL_MS, (unsigned)n);
    server.sendContent(head);

    static const struct { const char *key; uint8_t field; uint8_t dec; } SERIES[] = {
        { "boost", 0, 2 }, { "comb", 1, 1 }, { "lambda", 2, 3 }, { "iat", 3, 0 },
        { "oil", 4, 0 },   { "water", 5, 0 }, { "batt", 6, 1 },
    };
    for (uint8_t s = 0; s < 7; s++) {
        char chunk[512];
        size_t len = (size_t)snprintf(chunk, sizeof(chunk), "%s\"%s\":[", s ? "," : "", SERIES[s].key);
        for (uint16_t i = 0; i < n; i++) {
            history_sample_t smp;
            if (!history_get(i, &smp)) break;
            float v;
            switch (SERIES[s].field) {
            case 0: v = smp.boost_bar;         break;
            case 1: v = smp.comb_pressure_bar; break;
            case 2: v = smp.lambda;            break;
            case 3: v = smp.iat_c;             break;
            case 4: v = smp.oil_temp_c;        break;
            case 5: v = smp.water_temp_c;      break;
            default: v = smp.battery_v;        break;
            }
            if (i) chunk[len++] = ',';
            json_num(chunk, sizeof(chunk), &len, v, SERIES[s].dec);
            if (len > sizeof(chunk) - 32) {           /* a maior amostra cabe folgado */
                chunk[len] = '\0';
                server.sendContent(chunk);
                len = 0;
            }
        }
        chunk[len++] = ']';
        chunk[len] = '\0';
        server.sendContent(chunk);
    }
    server.sendContent("}}");
    server.sendContent("");
}

/* Os pedidos so vao para o CYD; quem decide se pode ler/apagar e ele, com as
 * mesmas travas da tela DTC do painel. */
static void handle_dtc_read(void)
{
    send_frame(LINK_READ);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handle_dtc_clear(void)
{
    send_frame(LINK_CLEAR);
    server.send(200, "application/json", "{\"ok\":true}");
}

/* Portal cativo: qualquer rota desconhecida (as sondas de conectividade do
 * Android e do iPhone) recebe 302 para a raiz, e o celular abre a pagina. */
static void handle_not_found(void)
{
    server.sendHeader("Location", hello.url, true);
    server.send(302, "text/plain", "");
}

static void dns_poll(void)
{
    const int n = dns.parsePacket();
    if (n <= 0) return;
    uint8_t b[300];
    const int len = dns.read(b, sizeof(b) - 16);
    const int r = captive_dns_answer(b, len, (int)sizeof(b), (uint32_t)ap_ip);
    if (r <= 0) return;
    dns.beginPacket(dns.remoteIP(), dns.remotePort());
    dns.write(b, (size_t)r);
    dns.endPacket();
}

/* ---- ciclo ------------------------------------------------------------------- */

void setup()
{
    Serial1.begin(115200);                    /* depuracao em D4 */
    Serial.setRxBufferSize(RX_BUFFER);        /* antes do begin: so vale assim */
    Serial.begin(LINK_BAUD);
    Serial.swap();                            /* UART0 em D7 (RX) / D8 (TX) */

    /* Rede derivada do MAC: estavel entre boots (o QR continua valendo) e
     * diferente por aparelho. */
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    snprintf(hello.ssid, sizeof(hello.ssid), "CarDash-%02X%02X", mac[4], mac[5]);
    snprintf(hello.pass, sizeof(hello.pass), "dash%02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf(hello.url, sizeof(hello.url), "http://%u.%u.%u.%u", ap_ip[0], ap_ip[1], ap_ip[2], ap_ip[3]);

    WiFi.persistent(false);                   /* nada de gravar na flash a cada boot */
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ap_ip, ap_ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(hello.ssid, hello.pass, AP_CHANNEL, 0, AP_MAX_CLIENTS);

    dns.begin(DNS_PORT);

    server.on("/", handle_root);
    server.on("/api/state", handle_state);
    server.on("/api/history", handle_history);
    server.on("/api/dtc", HTTP_GET, handle_dtc_list);
    server.on("/api/dtc/read", HTTP_POST, handle_dtc_read);
    server.on("/api/dtc/clear", HTTP_POST, handle_dtc_clear);
    server.onNotFound(handle_not_found);
    server.begin();

    history_reset();
    Serial1.printf("\n[WEMOS] %s / %s -> %s, heap %u\n", hello.ssid, hello.pass, hello.url,
                   (unsigned)ESP.getFreeHeap());
}

void loop()
{
    receive();
    dns_poll();
    server.handleClient();
    receive();                                /* o que chegou enquanto a pagina era servida */

    const uint32_t now = millis();
    if ((uint32_t)(now - last_hello_ms) >= LINK_HELLO_MS) {
        last_hello_ms = now;
        send_hello();
    }
    if ((uint32_t)(now - last_debug_ms) >= DEBUG_LOG_MS) {
        last_debug_ms = now;
        Serial1.printf("[WEMOS] CYD %s, quadros ok %lu ruins %lu, %u celular(es), historico %u, heap %u\n",
                       cyd_alive() ? "ok" : "SEM RESPOSTA",
                       (unsigned long)frames_ok, (unsigned long)frames_bad,
                       (unsigned)WiFi.softAPgetStationNum(), (unsigned)history_count(),
                       (unsigned)ESP.getFreeHeap());
    }
}
