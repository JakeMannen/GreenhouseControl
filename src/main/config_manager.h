#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Reporting thresholds
    int32_t report_threshold_battery_mv;
    int32_t report_threshold_panel_mv;
    int32_t report_threshold_charge_current_ma;
    int32_t report_threshold_panel_power_w;

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

} app_config_t;

/**
 * @brief Initialize the configuration manager and load settings from NVS.
 * If settings are missing, default values are populated and saved.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t config_manager_init(void);

/**
 * @brief Get a pointer to the loaded configuration.
 * 
 * @return const app_config_t* Pointer to the current config
 */
const app_config_t* config_manager_get(void);

/**
 * @brief Save the provided configuration to NVS and update the active config.
 * 
 * @param new_config Pointer to the new configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t config_manager_save(const app_config_t *new_config);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H
