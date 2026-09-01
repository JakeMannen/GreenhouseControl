#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GH_SWITCH_MODE_PRESS = 0, // Momentary press toggles pump / runs for set duration
    GH_SWITCH_MODE_HOLD = 1,  // Holding switch runs pump, releasing stops pump
} gh_switch_mode_t;

typedef struct {
    // Reporting thresholds
    int32_t report_threshold_battery_mv;
    int32_t report_threshold_panel_mv;
    int32_t report_threshold_charge_current_ma;
    int32_t report_threshold_panel_power_w;
    int32_t report_threshold_load_current_ma;

    // Reporting intervals
    int64_t report_interval_max_us; // e.g., 300,000,000 for 5 minutes
    int64_t report_interval_min_us; // e.g., 10,000,000 for 10 seconds

    // Battery percentage curve (mV for 100%, 90%, 70%, 40%, 20%, 0%)
    int32_t batt_curve_100_mv;
    int32_t batt_curve_90_mv;
    int32_t batt_curve_70_mv;
    int32_t batt_curve_40_mv;
    int32_t batt_curve_20_mv;
    int32_t batt_curve_0_mv;

    // External Switch Settings
    gh_switch_mode_t switch_mode;

} app_config_t;

/**
 * @brief Initialize the configuration manager and load settings from NVS.
 * 
 * If settings are missing, default values are populated and saved.
 * 
 * @return 
 *     - ESP_OK: Success.
 *     - Other error code: Failed.
 */
esp_err_t config_manager_init(void);

/**
 * @brief Get a pointer to the loaded configuration.
 * 
 * @return const app_config_t* Pointer to the current config.
 */
const app_config_t* config_manager_get(void);

/**
 * @brief Save the provided configuration to NVS and update the active config.
 * 
 * @param new_config Pointer to the new configuration.
 * @return 
 *     - ESP_OK: Success.
 *     - Other error code: Failed.
 */
esp_err_t config_manager_save(const app_config_t *new_config);

#ifdef __cplusplus
}
#endif
