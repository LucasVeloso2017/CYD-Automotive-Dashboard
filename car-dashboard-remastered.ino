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
#include "src/net/track_server.h"

/* Diagnostico: 1 = nao inicializa o Bluetooth. Se o celular passar a
 * conectar no AP com isso, a causa e a coexistencia de radio BT+Wi-Fi; se
 * continuar falhando, a causa esta na configuracao do proprio AP. */
#define TRACK_DEBUG_NO_BT  0

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define LVGL_BUFFER_LINES 16

#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_CLK  25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
#define TOUCH_X_MIN  200
#define TOUCH_X_MAX  3800
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX  3800

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

static uint8_t *draw_buf = NULL;

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

/* Heap e o recurso mais apertado deste projeto (BT Classic + LVGL + Wi-Fi
 * sem PSRAM). Loga a cada etapa do boot para ver quem consome o que. */
static void heap_log(const char *stage)
{
    Serial.printf("[HEAP] %-14s livre %6u B, maior bloco %6u B\n",
                  stage, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void setup()
{
    Serial.begin(115200);
    heap_log("boot");

    // A GRAM do painel acorda com lixo. tft.begin() liga o backlight no fim da
    // init(), entao esse lixo vira chuvisco colorido na tela ate o LVGL desenhar
    // o primeiro frame. Apaga o backlight, limpa a tela e so acende no fim do setup.
    tft.begin();
#if defined(TFT_BL) && defined(TFT_BACKLIGHT_ON)
    digitalWrite(TFT_BL, !TFT_BACKLIGHT_ON);
#endif
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSPI);
    ts.setRotation(1);
    heap_log("tft+touch");

#if TRACK_DEBUG_NO_BT
    Serial.println("[BT] DESLIGADO por TRACK_DEBUG_NO_BT (diagnostico Wi-Fi)");
#else
    bt_init();
#endif
    heap_log("bluetooth");

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    draw_buf = (uint8_t *)heap_caps_malloc(SCREEN_WIDTH * LVGL_BUFFER_LINES * (LV_COLOR_DEPTH / 8),
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!draw_buf) {
        Serial.println("Failed to allocate LVGL draw buffer");
        while (true) delay(1000);
    }

    lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_buffers(disp, draw_buf, NULL,
                            SCREEN_WIDTH * LVGL_BUFFER_LINES * (LV_COLOR_DEPTH / 8),
                            LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    heap_log("lvgl+display");

    ui_init();
    heap_log("ui (telas)");

    // Primeiro frame no painel antes de acender: a tela abre no splash, nunca no lixo.
    lv_refr_now(NULL);
#if defined(TFT_BL) && defined(TFT_BACKLIGHT_ON)
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif

    Serial.println("Car Dashboard initialized");
}

void loop()
{
    lv_timer_handler();
    bt_loop();
    track_server_loop();
    delay(2);
}
