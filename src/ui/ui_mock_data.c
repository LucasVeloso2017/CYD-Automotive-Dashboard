#include "ui.h"
#include "ui_mock_data.h"
#include "../gateway/bt_gateway.h"
#include "../gateway/elm327_gateway.h"
#include "../gateway/vehicle_data.h"

#include <stdio.h>

#define LOG_INTERVAL_MS    MOCK_LOG_INTERVAL_MS
#define BOOST_MAX_FLOOR    (-50.0f)

static float max_boost = BOOST_MAX_FLOOR;
static float max_comb = 0;
static float max_iat = 0;
static float max_oil = 0;
static float max_lambda = 0;
static float max_batt = 0;

static bool session_active = false;
static uint32_t last_log_ms = 0;
static uint32_t last_rev = 0;

static void fmt_f1(lv_obj_t *l, float v)
{
    if (!l) return;
    char b[12];
    int w = (int)v, f = (int)((v - w) * 10);
    if (f < 0) f = -f;
    if (v < 0 && w == 0) lv_snprintf(b, sizeof(b), "-%d.%d", w, f);
    else lv_snprintf(b, sizeof(b), "%d.%d", w, f);
    lv_label_set_text(l, b);
}

static void fmt_f2(lv_obj_t *l, float v)
{
    if (!l) return;
    char b[12];
    int w = (int)v, f = (int)((v - w) * 100);
    if (f < 0) f = -f;
    lv_snprintf(b, sizeof(b), "%d.%02d", w, f);
    lv_label_set_text(l, b);
}

static void fmt_int(lv_obj_t *l, int v)
{
    if (!l) return;
    char b[8];
    lv_snprintf(b, sizeof(b), "%d", v);
    lv_label_set_text(l, b);
}

static void update_max_values(float boost, float comb, float iat, float oil,
                              float lambda, float batt)
{
    if (boost > max_boost) max_boost = boost;
    if (comb > max_comb) max_comb = comb;
    if (iat > max_iat) max_iat = iat;
    if (oil > max_oil) max_oil = oil;
    if (lambda > max_lambda) max_lambda = lambda;
    if (batt > max_batt) max_batt = batt;
}

static void render_gauges(void)
{
    const vehicle_gauge_data_t *g = &vehicle.gauges;

    fmt_f2(ui_lambdanano, g->lambda);
    fmt_int(ui_temperaturenano, (int)g->water_temp_c);
    fmt_f1(ui_baterynano, g->battery_v);

    fmt_f1(ui_boosttrack, g->boost_bar);
    fmt_f1(ui_combpressuretrack, g->comb_pressure_bar);
    fmt_int(ui_iattrack, (int)g->iat_c);
    fmt_int(ui_oiltrack, (int)g->oil_temp_c);
    fmt_f2(ui_lambdatrack, g->lambda);
    fmt_f1(ui_baterytrack, g->battery_v);

    fmt_f1(ui_maxbostttrack1, max_boost <= BOOST_MAX_FLOOR ? 0.0f : max_boost);
    fmt_f1(ui_maxcombttrack, max_comb);
    fmt_f1(ui_maxiattrack1, max_iat);
    fmt_int(ui_maxoiltrack, (int)max_oil);
    fmt_f2(ui_maxlambdatrack, max_lambda);
    fmt_f1(ui_maxbaterytrack1, max_batt);
}

static void reset_max_values(void)
{
    max_boost = BOOST_MAX_FLOOR;
    max_comb = 0;
    max_iat = 0;
    max_oil = 0;
    max_lambda = 0;
    max_batt = 0;
}

static void update_default_values(void)
{
    lv_obj_t *labels[] = {
        ui_lambdanano,
        ui_temperaturenano,
        ui_baterynano,
        ui_boosttrack,
        ui_combpressuretrack,
        ui_iattrack,
        ui_oiltrack,
        ui_lambdatrack,
        ui_baterytrack,
        ui_maxbostttrack1,
        ui_maxcombttrack,
        ui_maxiattrack1,
        ui_maxoiltrack,
        ui_maxlambdatrack,
        ui_maxbaterytrack1,
    };

    for (uint8_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        if (labels[i]) lv_label_set_text(labels[i], "00");
    }
}

static void log_gauges(uint32_t now)
{
    if (now - last_log_ms < LOG_INTERVAL_MS) return;
    last_log_ms = now;

    const vehicle_gauge_data_t *g = &vehicle.gauges;
    printf(
        "[OBD2] boost=%.2f max=%.2f | comb=%.2f max=%.2f | iat=%.0f max=%.0f | "
        "oil=%.0f max=%.0f | lambda=%.3f max=%.3f | batt=%.1f max=%.1f | cool=%.0f\n",
        g->boost_bar, max_boost <= BOOST_MAX_FLOOR ? 0.0f : max_boost,
        g->comb_pressure_bar, max_comb,
        g->iat_c, max_iat,
        g->oil_temp_c, max_oil,
        g->lambda, max_lambda,
        g->battery_v, max_batt,
        g->water_temp_c
    );
}

static void poll_cb(lv_timer_t *timer)
{
    (void)timer;

    bool connected = bt_is_connected() && elm327_is_ready();

    if (!connected) {
        if (session_active) {
            session_active = false;
            last_rev = 0;
            reset_max_values();
            printf("[OBD2] Session ended - max values reset\n");
        }
        update_default_values();
        return;
    }

    if (!vehicle.gauges.valid) return;

    if (!session_active) {
        session_active = true;
        reset_max_values();
        printf("[OBD2] Session started\n");
    }

    if (vehicle.gauges.data_rev == last_rev) return;
    last_rev = vehicle.gauges.data_rev;

    const vehicle_gauge_data_t *g = &vehicle.gauges;
    update_max_values(g->boost_bar, g->comb_pressure_bar, g->iat_c,
                      g->oil_temp_c, g->lambda, g->battery_v);
    render_gauges();
    log_gauges(lv_tick_get());
}

void ui_mock_data_init(void)
{
    reset_max_values();
    session_active = false;
    last_log_ms = 0;
    last_rev = 0;
    update_default_values();
    lv_timer_create(poll_cb, MOCK_POLL_INTERVAL_MS, NULL);
}
