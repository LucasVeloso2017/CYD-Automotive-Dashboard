#ifndef VEHICLE_DATA_H
#define VEHICLE_DATA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float lambda;
    float water_temp_c;
    float battery_v;
    float boost_bar;
    float comb_pressure_bar;
    float iat_c;
    float oil_temp_c;
    bool  valid;
    volatile uint32_t data_rev;
} vehicle_gauge_data_t;

typedef struct {
    vehicle_gauge_data_t gauges;
} vehicle_data_t;

#ifdef __cplusplus
extern "C" {
#endif

extern vehicle_data_t vehicle;

#ifdef __cplusplus
}
#endif

#endif
