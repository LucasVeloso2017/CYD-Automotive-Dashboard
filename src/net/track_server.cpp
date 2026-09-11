#include "track_server.h"
#include "track_page.h"
#include "../gateway/bt_gateway.h"
#include "../ui/ui_mock_data.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <lvgl.h>

/* Medido no CYD: o AP + lwIP + servidor custam ~60 KB ao subir, e atender
 * um celular (handshake, DHCP, TCP) precisa de mais ~30 KB vivos. Abaixo
 * disso nem tentamos: esp_wifi_init sem memoria aborta e reseta a placa. */
#define TRACK_MIN_FREE_HEAP   (96 * 1024)

static WebServer server(80);
static DNSServer dns;
static bool running = false;
static bool failed = false;
static bool start_pending = false;
static bool stop_pending = false;
static bool events_hooked = false;

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

static void no_cache(void)
{
    server.sendHeader("Cache-Control", "no-store");
}

static void handle_root(void)
{
    no_cache();
    server.send_P(200, "text/html; charset=utf-8", TRACK_PAGE_HTML);
}

/* Resumo da ultima sessao (ou da atual). Chaves casam com CARDS[] no HTML.
 * Enquanto o Wi-Fi esta no ar o Bluetooth esta suspenso, entao na pratica
 * "active" e sempre false aqui -- fica para o caso de um dia coexistirem. */
static void handle_data(void)
{
    ui_session_stats_t s;
    ui_session_get_stats(&s);

    uint32_t now = lv_tick_get();
    bool has_session = s.samples > 0;
    uint32_t session_s = has_session ? ((s.active ? now : s.end_ms) - s.start_ms) / 1000 : 0;
    uint32_t ended_s_ago = (has_session && !s.active) ? (now - s.end_ms) / 1000 : 0;

    static char buf[512];   /* fora da stack da loopTask */
    snprintf(buf, sizeof(buf),
        "{\"session\":%s,\"active\":%s,\"session_s\":%lu,\"ended_s_ago\":%lu,\"samples\":%lu,"
        "\"max\":{\"boost\":%.2f,\"rail\":%.1f,\"iat\":%.0f,\"oil\":%.0f,"
                 "\"lambda\":%.2f,\"batt\":%.1f,\"water\":%.0f},"
        "\"min\":{\"lambda\":%.2f,\"batt\":%.1f}}",
        has_session ? "true" : "false",
        s.active ? "true" : "false",
        (unsigned long)session_s, (unsigned long)ended_s_ago, (unsigned long)s.samples,
        s.max_boost, s.max_comb, s.max_iat, s.max_oil,
        s.max_lambda, s.max_batt, s.max_water,
        s.min_lambda, s.min_batt);

    no_cache();
    server.send(200, "application/json", buf);
}

/* Captive portal: qualquer URL que nao seja nossa (incluindo as sondas de
 * conectividade do Android/iOS/Windows) redireciona para a pagina. Como o DNS
 * responde AP_IP para tudo, o sistema do celular ve "portal" e abre sozinho. */
static void handle_portal(void)
{
    server.sendHeader("Location", TRACK_AP_URL "/", true);
    no_cache();
    server.send(302, "text/plain", "");
}

/* Diagnostico no serial: em que etapa a associacao do celular para.
 * reason: 2 auth expirou, 15/204 timeout do handshake WPA, 202 auth falhou,
 * 203 assoc falhou, 8 o proprio celular saiu. */
static void wifi_event_cb(arduino_event_id_t ev, arduino_event_info_t info)
{
    switch (ev) {
    case ARDUINO_EVENT_WIFI_AP_START:
        Serial.println("[TRACK] evento: AP no ar");
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
        const uint8_t *m = info.wifi_ap_staconnected.mac;
        Serial.printf("[TRACK] evento: celular associou %02X:%02X:%02X:%02X:%02X:%02X, heap livre %u B\n",
                      m[0], m[1], m[2], m[3], m[4], m[5], ESP.getFreeHeap());
        break;
    }
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        Serial.printf("[TRACK] evento: IP entregue %s\n",
                      IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
        const uint8_t *m = info.wifi_ap_stadisconnected.mac;
        Serial.printf("[TRACK] evento: celular saiu %02X:%02X:%02X:%02X:%02X:%02X, reason %u\n",
                      m[0], m[1], m[2], m[3], m[4], m[5], info.wifi_ap_stadisconnected.reason);
        break;
    }
    default:
        break;
    }
}

void track_server_request_start(void)
{
    if (!running && !failed) { start_pending = true; stop_pending = false; }
}

void track_server_request_stop(void)
{
    start_pending = false;
    if (running) stop_pending = true;
}

void track_server_start(void)
{
    if (running) return;
    failed = false;

    if (!events_hooked) {
        WiFi.onEvent(wifi_event_cb);
        events_hooked = true;
    }

    uint32_t free_heap = ESP.getFreeHeap();
    Serial.printf("[TRACK] subindo AP: heap livre %u B, maior bloco %u B\n", free_heap, ESP.getMaxAllocHeap());
    if (free_heap < TRACK_MIN_FREE_HEAP) {
        Serial.printf("[TRACK] heap insuficiente (< %u B), AP nao iniciado\n", (unsigned)TRACK_MIN_FREE_HEAP);
        failed = true;
        return;
    }

    if (!WiFi.mode(WIFI_AP)) {
        Serial.println("[TRACK] WiFi.mode(WIFI_AP) falhou");
        WiFi.mode(WIFI_OFF);
        failed = true;
        return;
    }

    /* Configura antes de subir o AP. O ultimo parametro (dns = AP_IP) e
     * essencial: sem ele o DHCP nao anuncia DNS ao celular, as sondas de
     * captive portal nunca chegam ao nosso DNSServer e a pagina nao abre. */
    if (!WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK, IPAddress(0, 0, 0, 0), AP_IP)) {
        Serial.println("[TRACK] WiFi.softAPConfig falhou");
    }
    if (!WiFi.softAP(TRACK_AP_SSID, TRACK_AP_PASS, 1, false, 4)) {
        Serial.println("[TRACK] WiFi.softAP falhou");
        WiFi.mode(WIFI_OFF);
        failed = true;
        return;
    }

    /* DHCP option 114 (RFC 8910): a URL do portal vai no proprio DHCP.
     * iOS 14+ e Android 11+ abrem a pagina por aqui; as sondas HTTP ficam
     * como fallback para celulares mais antigos. */
    if (!WiFi.AP.enableDhcpCaptivePortal()) {
        Serial.println("[TRACK] DHCP captive portal (opt 114) nao habilitado; seguindo so com DNS+HTTP");
    }

    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", AP_IP);

    server.on("/", handle_root);
    server.on("/data", handle_data);

    static const char *const probes[] = {
        "/generate_204", "/gen_204",                            /* Android */
        "/hotspot-detect.html", "/library/test/success.html",   /* iOS / macOS */
        "/connecttest.txt", "/ncsi.txt", "/fwlink", "/redirect", /* Windows */
        "/success.txt", "/canonical.html",                      /* Firefox / Ubuntu */
    };
    for (const char *p : probes) server.on(p, handle_portal);
    server.onNotFound(handle_portal);

    server.begin();
    running = true;

    Serial.printf("[TRACK] AP '%s' ativo, pagina em http://%s/, heap livre: %u B (AP custou %u B)\n",
                  TRACK_AP_SSID, WiFi.softAPIP().toString().c_str(),
                  ESP.getFreeHeap(), free_heap - ESP.getFreeHeap());
}

void track_server_stop(void)
{
    if (!running) return;
    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    running = false;
    failed = false;
    Serial.printf("[TRACK] AP desligado, heap livre: %u B\n", ESP.getFreeHeap());
}

/* Roda no topo da loopTask. A troca BT <-> Wi-Fi acontece aqui, nunca de
 * dentro de um callback do LVGL: suspender o BT espera a task do ELM sair
 * e inicializar Wi-Fi precisa de pilha rasa. */
void track_server_loop(void)
{
    if (start_pending) {
        if (!bt_suspend()) return;          /* connect BT em curso: tenta na proxima volta */
        start_pending = false;
        track_server_start();
        if (failed) bt_resume();            /* sem Wi-Fi, o painel volta ao normal */
    }
    if (stop_pending) {
        stop_pending = false;
        track_server_stop();
        bt_resume();
    }
    if (!running) return;
    dns.processNextRequest();
    server.handleClient();
}

bool track_server_running(void)
{
    return running;
}

bool track_server_failed(void)
{
    return failed;
}

uint8_t track_server_clients(void)
{
    return running ? (uint8_t)WiFi.softAPgetStationNum() : 0;
}
