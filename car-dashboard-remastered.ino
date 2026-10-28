/*
 * Car Dashboard Remastered - ESP32 CYD + LVGL 9.x
 *
 * Dependencias: lvgl (9.x), TFT_eSPI, XPT2046_Touchscreen
 *
 * Arduino IDE (CYD):
 *   Board:            ESP32 Dev Module
 *   Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
 *   Upload Speed:     115200   (921600 falha no CH340 de muitos CYD)
 *   Flash Mode:       QIO
 *   Flash Size:       4MB
 *
 * Se o upload falhar: feche o Serial Monitor, segure BOOT, clique Upload,
 * solte BOOT quando aparecer "Connecting...".
 */

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <esp_heap_caps.h>

#include "src/ui/ui.h"
#include "src/gateway/bt_gateway.h"
#include "src/power/battery.h"
#include "src/display/display.h"
#if UI_ENABLE_WEB
#include "src/net/link.h"
#endif

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define LVGL_BUFFER_LINES 16   // por buffer: 320 x 16 x 2 = 10 kB

// Envio para o display por DMA, com dois buffers: enquanto o SPI manda um
// pedaco da tela, o LVGL ja desenha o seguinte no outro buffer. Sem DMA as duas
// coisas acontecem uma depois da outra -- e num slide, que redesenha a tela
// inteira a cada quadro, so o SPI ja leva ~22 ms por quadro (153,6 kB a 55 MHz).
// 0 volta ao envio bloqueante de antes, com um buffer so (use se o display
// mostrar faixas ou pedacos trocados).
#define LCD_USE_DMA       1

// Periodo de atualizacao do display. O padrao do lv_conf.h e 33 ms (30 fps);
// 16 ms deixa o slide chegar ao que o SPI aguenta. Fica aqui, e nao no
// lv_conf.h, porque aquele arquivo e compartilhado por todos os sketches.
#define LVGL_REFR_MS      16

#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_CLK  25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
// Entrada do amplificador de audio (LTK8002D, conector SPEAK). O projeto nao
// usa som, mas o pino precisa de nivel definido: solto, ele capta o ruido do
// SPI da tela e do Bluetooth, o amplificador amplifica esse ruido o tempo todo
// e esquenta. Preso em LOW nao ha o que amplificar; a entrada do amplificador e
// acoplada por capacitor, entao o nivel fixo nao gera som nem corrente.
#define SPEAKER_PIN  26

#define TOUCH_X_MIN  200
#define TOUCH_X_MAX  3800
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX  3800

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

static uint8_t *draw_buf = NULL;
static uint8_t *draw_buf2 = NULL;

// Onde a memoria foi parar: no CYD (sem PSRAM) Bluedroid e LVGL dividem a mesma
// DRAM. Imprimir por etapa mostra quem levou o que.
static void log_heap(const char *when)
{
    Serial.printf("[HEAP] %-22s livre %6u  maior bloco %6u\n", when,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

// Medicao de desempenho do display no serial, a cada PERF_LOG_MS:
//   ciclos/s   quantas vezes o LVGL verificou a tela (o teto: ~60 com 16 ms);
//   quadros/s  quantos ciclos tinham algo para desenhar;
//   desenho    tempo medio de um quadro (render + envio), o que diz se cabe 60.
// "quadros/s" baixo com "desenho" curto e ciclos ~60 nao e falta de CPU: e a
// tela sem nada mudando (um numero com uma casa decimal muda poucas vezes por
// segundo). Desenho perto de 16 ms ou ciclos bem abaixo de 60 e o display.
#define PERF_LOG_MS 5000
static uint32_t perf_t0 = 0, perf_cycles = 0, perf_frames = 0, perf_busy_us = 0;
static uint32_t perf_render_t0 = 0;
static bool perf_rendering = false;

static void perf_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RENDER_START) {
        if (!perf_rendering) perf_render_t0 = micros();   // varias areas num ciclo: conta do primeiro
        perf_rendering = true;
        return;
    }
    // LV_EVENT_REFR_READY: fim do ciclo, desenhando ou nao
    perf_cycles++;
    if (perf_rendering) {
#if LCD_USE_DMA
        tft.dmaWait();          // o ultimo pedaco ainda pode estar saindo: entra no tempo do quadro
#endif
        perf_busy_us += micros() - perf_render_t0;
        perf_frames++;
        perf_rendering = false;
    }
    const uint32_t now = millis();
    if (perf_t0 == 0) perf_t0 = now;
    if (now - perf_t0 < PERF_LOG_MS) return;
    const uint32_t span = now - perf_t0;
    Serial.printf("[LVGL] ciclos %u/s | quadros %u/s | desenho %.1f ms/quadro\n",
                  (unsigned)(perf_cycles * 1000u / span), (unsigned)(perf_frames * 1000u / span),
                  perf_frames ? perf_busy_us / 1000.0f / perf_frames : 0.0f);
    perf_t0 = now;
    perf_cycles = perf_frames = perf_busy_us = 0;
}

#if LCD_USE_DMA
// O DMA manda este buffer em segundo plano e o LVGL ja recebe o "pronto": com
// dois buffers, o proximo pedaco e desenhado no outro, nunca neste. Quem
// garante a ordem e o dmaWait(): mexer na janela de enderecos com um envio
// em andamento corromperia a tela, entao a janela so muda depois que o
// envio anterior terminou. (O pushPixelsDMA tambem espera, mas a janela vem antes.)
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    tft.dmaWait();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixelsDMA((uint16_t *)px_map, w * h);
    lv_display_flush_ready(disp);
}
#else
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(disp);
}
#endif

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int16_t last_x = 0;
    static int16_t last_y = 0;

    if (!ts.tirqTouched() || !ts.touched()) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = last_x;
        data->point.y = last_y;
        return;
    }

    TS_Point p = ts.getPoint();
    last_x = (int16_t)constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_WIDTH - 1), 0, SCREEN_WIDTH - 1);
    last_y = (int16_t)constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_HEIGHT - 1), 0, SCREEN_HEIGHT - 1);
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = last_x;
    data->point.y = last_y;
}

static uint32_t tick_get_cb(void) { return millis(); }

void setup()
{
    // Primeira coisa do boot: quanto menos tempo a entrada do amplificador
    // fica solta, menos ruido ele amplifica.
    pinMode(SPEAKER_PIN, OUTPUT);
    digitalWrite(SPEAKER_PIN, LOW);

    Serial.begin(115200);
    delay(200);                  // deixa o monitor serial pegar as primeiras linhas
    log_heap("no boot");

    // A GRAM do painel acorda com lixo. tft.begin() liga o backlight no fim da
    // init(), entao esse lixo vira chuvisco colorido na tela ate o LVGL desenhar
    // o primeiro frame. Apaga o backlight, limpa a tela e so acende no fim do setup.
    tft.begin();
#if defined(TFT_BL) && defined(TFT_BACKLIGHT_ON)
    digitalWrite(TFT_BL, !TFT_BACKLIGHT_ON);
#endif
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
#if LCD_USE_DMA
    tft.initDMA();
    tft.setSwapBytes(true);     // o pushPixelsDMA troca os bytes do RGB565 como o pushColors(..., true)
    tft.startWrite();           // CS do display fica baixo: o DMA exige, e o toque esta em outro barramento SPI
#endif

    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSPI);
    // Rotacao salva na tela de configuracoes (normal ou 180 graus), para painel
    // e toque juntos; o PWM do backlight nasce aqui, ainda apagado.
    display_init();

    bt_init();
    log_heap("apos bt_init");
    battery_init();

#if UI_ENABLE_WEB
    // Serial para o Wemos D1 mini, que serve a pagina web: o WiFi mora la, nao
    // aqui (Bluetooth e WiFi nao cabiam juntos no CYD).
    link_init();
#endif

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    const size_t buf_bytes = SCREEN_WIDTH * LVGL_BUFFER_LINES * (LV_COLOR_DEPTH / 8);
#if LCD_USE_DMA
    // Memoria que o DMA do SPI alcanca (DRAM interna); dois buffers alternados.
    draw_buf  = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    draw_buf2 = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
#else
    draw_buf  = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    if (!draw_buf || (LCD_USE_DMA && !draw_buf2)) {
        Serial.println("Failed to allocate LVGL draw buffer");
        while (true) delay(1000);
    }

    lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_buffers(disp, draw_buf, draw_buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_timer_set_period(lv_display_get_refr_timer(disp), LVGL_REFR_MS);
    lv_display_add_event_cb(disp, perf_event_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, perf_event_cb, LV_EVENT_REFR_READY, NULL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);

    ui_init();
    log_heap("apos ui_init");

    // Primeiro frame no painel antes de acender: a tela abre no splash, nunca no lixo.
    lv_refr_now(NULL);
#if LCD_USE_DMA
    tft.dmaWait();              // o ultimo pedaco do primeiro quadro ainda pode estar saindo
#endif
    display_backlight_on();     // no brilho salvo, nao no maximo

    log_heap("fim do setup");
    Serial.println("Car Dashboard initialized");
}

void loop()
{
    lv_timer_handler();
    bt_loop();
    battery_loop();
#if UI_ENABLE_WEB
    link_loop();
#endif
    // 1 ms, nao 2: o refresh de 16 ms so roda quando o lv_timer_handler e
    // chamado, e o atraso desta pausa se soma a cada quadro (16 + 2 = 55 fps).
    delay(1);
}
