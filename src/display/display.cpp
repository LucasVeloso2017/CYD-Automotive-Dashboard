#include "display.h"

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

/* Os dois vivem no sketch (car-dashboard-remastered.ino). */
extern TFT_eSPI tft;
extern XPT2046_Touchscreen ts;

#define NVS_NAMESPACE   "display"
#define NVS_BRIGHTNESS  "bright"
#define NVS_FLIPPED     "flip"

/* 5 kHz fica acima do que o olho percebe como piscar e longe da faixa audivel
 * no amplificador; 8 bits dao 256 degraus, mais que o slider usa. */
#define BL_PWM_HZ       5000
#define BL_PWM_BITS     8

#define ROTATION_NORMAL   1   /* paisagem, conector USB a direita */
#define ROTATION_FLIPPED  3   /* a mesma paisagem, 180 graus */

static uint8_t _brightness = DISPLAY_BRIGHTNESS_MAX;
static bool    _flipped = false;
static bool    _lit = false;

static uint8_t clamp_pct(int pct)
{
    if (pct < DISPLAY_BRIGHTNESS_MIN) return DISPLAY_BRIGHTNESS_MIN;
    if (pct > DISPLAY_BRIGHTNESS_MAX) return DISPLAY_BRIGHTNESS_MAX;
    return (uint8_t)pct;
}

static void write_backlight(uint8_t pct)
{
#if defined(TFT_BL)
    uint32_t duty = (uint32_t)pct * ((1u << BL_PWM_BITS) - 1) / 100u;
#if defined(TFT_BACKLIGHT_ON) && (TFT_BACKLIGHT_ON == LOW)
    duty = ((1u << BL_PWM_BITS) - 1) - duty;      /* backlight ativo em nivel baixo */
#endif
    ledcWrite(TFT_BL, duty);
#else
    (void)pct;
#endif
}

static void apply_rotation(void)
{
    const uint8_t r = _flipped ? ROTATION_FLIPPED : ROTATION_NORMAL;
    tft.dmaWait();          /* o MADCTL nao pode entrar no meio de um envio por DMA */
    tft.setRotation(r);
    ts.setRotation(r);      /* o toque gira junto, senao o dedo cai no lado oposto */
}

void display_init(void)
{
    Preferences p;
    if (p.begin(NVS_NAMESPACE, true)) {
        _brightness = clamp_pct(p.getUChar(NVS_BRIGHTNESS, DISPLAY_BRIGHTNESS_MAX));
        _flipped = p.getBool(NVS_FLIPPED, false);
        p.end();
    }

#if defined(TFT_BL)
    ledcAttach(TFT_BL, BL_PWM_HZ, BL_PWM_BITS);
    _lit = false;
    write_backlight(0);
#endif
    apply_rotation();
    Serial.printf("[DISPLAY] brilho %u%%, %s\n", (unsigned)_brightness, _flipped ? "girada 180" : "normal");
}

void display_backlight_on(void)
{
    _lit = true;
    write_backlight(_brightness);
}

uint8_t display_brightness(void) { return _brightness; }

void display_set_brightness(uint8_t pct)
{
    _brightness = clamp_pct(pct);
    if (_lit) write_backlight(_brightness);
}

void display_save_brightness(void)
{
    Preferences p;
    if (!p.begin(NVS_NAMESPACE, false)) return;
    if (p.getUChar(NVS_BRIGHTNESS, 0) != _brightness) p.putUChar(NVS_BRIGHTNESS, _brightness);
    p.end();
}

bool display_flipped(void) { return _flipped; }

void display_set_flipped(bool flipped)
{
    if (flipped == _flipped) return;
    _flipped = flipped;
    apply_rotation();

    Preferences p;
    if (p.begin(NVS_NAMESPACE, false)) {
        p.putBool(NVS_FLIPPED, _flipped);
        p.end();
    }
}
