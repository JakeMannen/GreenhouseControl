#pragma once

#include "ve_direct.h"

typedef struct {
    int16_t temperature;
    int16_t previous_temperature;
    int16_t humidity;
    int16_t previous_humidity;
} gh_climate_data_t;

typedef void (*climate_report_callback_t)(gh_climate_data_t *data);