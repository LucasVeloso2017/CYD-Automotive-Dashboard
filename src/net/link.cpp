#include "link.h"
#include "link_proto.h"

#include "../gateway/bt_gateway.h"
#include "../gateway/dtc_data.h"
#include "../gateway/dtc_describe.h"
#include "../gateway/elm327_gateway.h"
#include "../gateway/vehicle_data.h"
#include "../gateway/vehicle_profile.h"
#include "../ui/ui_mock_data.h"

#include <Arduino.h>
#include <stdarg.h>

/* GPIO 27 e 22 sao os dois pinos livres no conector CN1 do CYD (que tambem
 * tem 3,3 V e GND). Os outros estao com a tela (2, 12-15, 21), o toque (25,
 * 32, 33, 36, 39) e a bateria (35). Serial2 nos pinos padrao (16/17) cairia
 * no LED RGB. */
#define LINK_TX_PIN   27
#define LINK_RX_PIN   22

/* O quadro D chega a ~5 kB. Com buffer de envio, o write() so copia e volta:
 * o driver da UART escoa em segundo plano (5 kB a 115200 = ~0,45 s) e o LVGL
 * nao engasga esperando o fio. */
#define LINK_TX_BUFFER  6144
#define LINK_RX_LINE    160     /* so chegam W, R e X, todos curtos */

static HardwareSerial &_port = Serial2;

static char     _rx[LINK_RX_LINE];
static size_t   _rx_len = 0;
static bool     _rx_overflow = false;

static uint32_t _last_state_ms = 0;
static uint32_t _last_hello_ms = 0;
static bool     _hello_seen = false;
static bool     _was_alive = false;

/* O que ja foi mandado no D: reenvia quando muda, ou quando o Wemos volta. */
static uint32_t _sent_dtc_read_ms = 0;
static bool     _sent_dtc_valid = false;
static bool     _dtc_dirty = true;
static uint32_t _last_dtc_ms = 0;

static link_hello_t _hello;
static char _qr[LINK_SSID_MAX + LINK_PASS_MAX + 24];

/* ---- escrita de quadros ---------------------------------------------------
 *
 * Em fluxo: cada pedaco vai direto para a UART e entra no XOR corrente, sem
 * montar o quadro inteiro na RAM. */

static uint8_t _fx;

static void fw_raw(const char *s, size_t n)
{
    _fx ^= link_xor(s, n);
    _port.write((const uint8_t *)s, n);
}

static void fw_begin(char type)
{
    _fx = 0;
    fw_raw(&type, 1);
}

static void fw_printf(const char *fmt, ...)
{
    char b[192];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (n > 0) fw_raw(b, (size_t)n < sizeof(b) ? (size_t)n : sizeof(b) - 1);
}

/* NaN e infinito nao existem em JSON: viram null e o grafico pula o ponto. */
static void fw_num(float v, int dec)
{
    if (!isfinite(v)) fw_raw("null", 4);
    else              fw_printf("%.*f", dec, (double)v);
}

static void fw_str(const char *s)
{
    fw_raw("\"", 1);
    for (; s && *s; s++) {
        switch (*s) {
        case '"':  fw_raw("\\\"", 2); break;
        case '\\': fw_raw("\\\\", 2); break;
        case '\n': fw_raw("\\n", 2);  break;
        case '\r': break;
        default:
            if ((unsigned char)*s < 0x20) fw_raw(" ", 1);
            else                          fw_raw(s, 1);
        }
    }
    fw_raw("\"", 1);
}

static void fw_end(void)
{
    static const char DIGITS[] = "0123456789ABCDEF";   /* nao "HEX": macro do Arduino */
    const char tail[4] = { '*', DIGITS[_fx >> 4], DIGITS[_fx & 0x0F], '\n' };
    _port.write((const uint8_t *)tail, sizeof(tail));
}

/* ---- S: estado ------------------------------------------------------------ */

static bool session_up(void)
{
    return bt_is_connected() && elm327_is_ready();
}

static void send_state(void)
{
    const vehicle_gauge_data_t g = vehicle.gauges;
    vehicle_gauge_data_t mx;
    ui_mock_data_session_max(&mx);

    dtc_snapshot_t d;
    elm327_dtc_copy(&d);
    uint8_t nrc = 0;
    const dtc_clear_result_t clear = elm327_dtc_clear_result(&nrc);

    fw_begin(LINK_STATE);
    fw_printf("{\"bt\":%s,\"link\":%s,\"valid\":%s,\"profile\":",
              bt_is_connected() ? "true" : "false",
              session_up() ? "true" : "false",
              g.valid ? "true" : "false");
    fw_str(vehicle_profile()->name);

    fw_printf(",\"g\":{\"boost\":");   fw_num(g.boost_bar, 2);
    fw_printf(",\"comb\":");           fw_num(g.comb_pressure_bar, 1);
    fw_printf(",\"lambda\":");         fw_num(g.lambda, 3);
    fw_printf(",\"iat\":");            fw_num(g.iat_c, 0);
    fw_printf(",\"oil\":");            fw_num(g.oil_temp_c, 0);
    fw_printf(",\"water\":");          fw_num(g.water_temp_c, 0);
    fw_printf(",\"batt\":");           fw_num(g.battery_v, 1);

    fw_printf("},\"max\":{\"boost\":"); fw_num(mx.boost_bar, 2);
    fw_printf(",\"comb\":");            fw_num(mx.comb_pressure_bar, 1);
    fw_printf(",\"lambda\":");          fw_num(mx.lambda, 3);
    fw_printf(",\"iat\":");             fw_num(mx.iat_c, 0);
    fw_printf(",\"oil\":");             fw_num(mx.oil_temp_c, 0);
    fw_printf(",\"water\":");           fw_num(mx.water_temp_c, 0);
    fw_printf(",\"batt\":");            fw_num(mx.battery_v, 1);

    fw_printf("},\"dtc\":{\"valid\":%s,\"mil\":%s,\"op\":%d,"
              "\"stored\":%u,\"pending\":%u,\"permanent\":%u,"
              "\"read_ms\":%lu,\"clear\":%d,\"nrc\":%u}}",
              d.valid ? "true" : "false",
              d.mil ? "true" : "false",
              (int)elm327_dtc_op(),
              (unsigned)d.n_stored, (unsigned)d.n_pending, (unsigned)d.n_permanent,
              (unsigned long)d.read_ms, (int)clear, (unsigned)nrc);
    fw_end();

    /* Lista de DTC quando mudou, e de tempos em tempos mesmo sem mudar: e o
     * quadro grande, nao precisa ir a cada meio segundo, mas se um se perder no
     * fio (o Wemos ocupado servindo a pagina, a UART dele cheia) a pagina nao
     * pode ficar com a lista velha ate a proxima leitura. Durante uma leitura
     * (op != IDLE) o snapshot esta pela metade e espera. */
    if (elm327_dtc_op() == DTC_OP_IDLE &&
        (d.read_ms != _sent_dtc_read_ms || d.valid != _sent_dtc_valid ||
         (uint32_t)(millis() - _last_dtc_ms) >= LINK_DTC_REFRESH_MS)) {
        _dtc_dirty = true;
    }
}

/* ---- G: amostra para o historico do Wemos --------------------------------- */

static void send_sample(void)
{
    const vehicle_gauge_data_t g = vehicle.gauges;
    link_sample_t s;
    s.valid = session_up() && g.valid;
    s.v[LINK_F_LAMBDA] = g.lambda;
    s.v[LINK_F_WATER]  = g.water_temp_c;
    s.v[LINK_F_BATT]   = g.battery_v;
    s.v[LINK_F_BOOST]  = g.boost_bar;
    s.v[LINK_F_COMB]   = g.comb_pressure_bar;
    s.v[LINK_F_IAT]    = g.iat_c;
    s.v[LINK_F_OIL]    = g.oil_temp_c;

    char b[96];
    const int n = link_sample_format(b, sizeof(b), &s);
    if (n > 0 && link_seal(b, (size_t)n, sizeof(b)) > 0) _port.print(b);
}

/* ---- D: lista de DTC ------------------------------------------------------ */

/* Descricao estrutural do codigo, igual a da tela DTC do painel. */
static void describe_into(uint16_t raw, char *out, size_t cap)
{
    dtc_info_t info;
    dtc_describe(raw, &info);

    int n = snprintf(out, cap, "%s - %s", info.system,
                     info.generic ? "generico SAE" : "do fabricante");
    if (info.subsystem && n > 0 && (size_t)n < cap)
        n += snprintf(out + n, cap - (size_t)n, ". %s", info.subsystem);
    if (n > 0 && (size_t)n < cap) {
        if (info.cylinder > 0)
            snprintf(out + n, cap - (size_t)n, ", cilindro %d", info.cylinder);
        else if (info.cylinder < 0)
            snprintf(out + n, cap - (size_t)n, ", aleatoria ou em varios cilindros");
    }
}

static void send_dtc(void)
{
    dtc_snapshot_t d;
    elm327_dtc_copy(&d);

    fw_begin(LINK_DTC);
    fw_raw("{\"codes\":[", 10);

    const struct { const uint16_t *list; uint8_t n; const char *tag; } groups[] = {
        { d.stored,    d.n_stored,    "stored"    },
        { d.pending,   d.n_pending,   "pending"   },
        { d.permanent, d.n_permanent, "permanent" },
    };
    bool first = true;
    for (uint8_t gi = 0; gi < 3; gi++) {
        for (uint8_t i = 0; i < groups[gi].n; i++) {
            char code[6];
            char desc[180];
            dtc_format(groups[gi].list[i], code);
            describe_into(groups[gi].list[i], desc, sizeof(desc));
            fw_printf("%s{\"c\":", first ? "" : ",");
            fw_str(code);
            fw_raw(",\"t\":", 5);
            fw_str(groups[gi].tag);
            fw_raw(",\"d\":", 5);
            fw_str(desc);
            fw_raw("}", 1);
            first = false;
        }
    }

    fw_raw("],\"ff\":", 7);
    if (d.ff.valid) {
        char code[6];
        dtc_format(d.ff.dtc, code);
        fw_raw("{\"dtc\":", 7);
        fw_str(code);
        fw_printf(",\"rpm\":%u,\"speed\":%u,\"coolant\":%d,\"load\":%u,\"throttle\":%u,\"map\":%u}",
                  (unsigned)d.ff.rpm, (unsigned)d.ff.speed_kmh, (int)d.ff.coolant_c,
                  (unsigned)d.ff.load_pct, (unsigned)d.ff.throttle_pct, (unsigned)d.ff.map_kpa);
    } else {
        fw_raw("null", 4);
    }
    fw_printf(",\"read_ms\":%lu}", (unsigned long)d.read_ms);
    fw_end();

    _sent_dtc_read_ms = d.read_ms;
    _sent_dtc_valid = d.valid;
    _dtc_dirty = false;
    _last_dtc_ms = millis();
}

/* ---- recepcao -------------------------------------------------------------- */

/* Pedidos de DTC vindos da pagina passam pelas mesmas travas da tela do painel:
 * so com o ELM327 pronto e ocioso, e apagar so com codigos lidos para apagar.
 * Sem isto, um pedido feito com o ELM327 desconectado ficaria pendurado na
 * gateway e rodaria na proxima conexao — inclusive um apagamento. */
static void handle_line(char *line, size_t len)
{
    const int body = link_verify(line, len);
    if (body <= 0) return;

    switch (line[0]) {
    case LINK_HELLO: {
        link_hello_t h;
        if (link_hello_parse(line, &h) != 0) return;
        _hello = h;
        _hello_seen = true;
        _last_hello_ms = millis();
        snprintf(_qr, sizeof(_qr), "WIFI:T:WPA;S:%s;P:%s;;", _hello.ssid, _hello.pass);
        return;
    }
    case LINK_READ:
        if (session_up() && elm327_dtc_op() == DTC_OP_IDLE) {
            Serial.println("[LINK] pagina pediu leitura de DTC");
            elm327_request_dtc_read();
        }
        return;
    case LINK_CLEAR: {
        dtc_snapshot_t d;
        elm327_dtc_copy(&d);
        if (session_up() && elm327_dtc_op() == DTC_OP_IDLE && d.valid &&
            (d.n_stored + d.n_pending) > 0) {
            Serial.println("[LINK] pagina pediu apagamento de DTC");
            elm327_request_dtc_clear();
        }
        return;
    }
    default:
        return;
    }
}

static void receive(void)
{
    while (_port.available() > 0) {
        const char c = (char)_port.read();
        if (c == '\n') {
            if (!_rx_overflow && _rx_len > 0) {
                _rx[_rx_len] = '\0';
                handle_line(_rx, _rx_len);
            }
            _rx_len = 0;
            _rx_overflow = false;
        } else if (_rx_len + 1 < sizeof(_rx)) {
            _rx[_rx_len++] = c;
        } else {
            _rx_overflow = true;      /* descarta ate o proximo '\n' */
        }
    }
}

/* ---- ciclo ----------------------------------------------------------------- */

void link_init(void)
{
    memset(&_hello, 0, sizeof(_hello));
    _qr[0] = '\0';
    _port.setTxBufferSize(LINK_TX_BUFFER);     /* antes do begin: so vale assim */
    _port.setRxBufferSize(512);
    _port.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
    Serial.printf("[LINK] serial para o Wemos: TX GPIO %d, RX GPIO %d, %d baud\n",
                  LINK_TX_PIN, LINK_RX_PIN, LINK_BAUD);
}

void link_loop(void)
{
    receive();

    /* Wemos que aparece (ou volta depois de reiniciar) recebe a lista de DTC
     * de novo: ele nao guarda nada entre boots. */
    const bool alive = link_portal_alive();
    if (alive && !_was_alive) {
        _dtc_dirty = true;
        Serial.printf("[LINK] Wemos no ar: %s / %s -> %s\n", _hello.ssid, _hello.pass, _hello.url);
    } else if (!alive && _was_alive) {
        Serial.println("[LINK] Wemos sem resposta");
    }
    _was_alive = alive;

    /* Cadencia sem deriva: o proximo envio e marcado a partir do anterior, nao
     * de agora. Com "_last = now" cada periodo ganhava o atraso da loop() (5 a
     * 20 ms com o LVGL desenhando) e os 250 ms viravam 260-270. Se a loop() ficar
     * mais de um periodo para tras, realinha em vez de mandar uma rajada. */
    const uint32_t now = millis();
    if ((uint32_t)(now - _last_state_ms) >= LINK_STATE_MS) {
        _last_state_ms += LINK_STATE_MS;
        if ((uint32_t)(now - _last_state_ms) >= LINK_STATE_MS) _last_state_ms = now;
        send_state();
        send_sample();
        if (_dtc_dirty) send_dtc();
    }
}

bool link_portal_alive(void)
{
    return _hello_seen && (uint32_t)(millis() - _last_hello_ms) < LINK_STALE_MS;
}

const char *link_portal_ssid(void)       { return _hello.ssid; }
const char *link_portal_password(void)   { return _hello.pass; }
const char *link_portal_url(void)        { return _hello.url; }
uint8_t     link_portal_clients(void)    { return link_portal_alive() ? _hello.clients : 0; }
const char *link_portal_qr_payload(void) { return _qr; }
