#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "hal/gpio_types.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pins and timeouts are configured via Kconfig / config_manager

/**
 * @brief Initialize the GPIOs for the pump and button.
 * 
 * @param pump_state_changed_cb Callback function to notify other modules (e.g. Zigbee) of a pump state change.
 * @param factory_reset_cb Callback function triggered when a factory reset sequence is detected (3 quick button presses).
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t gpio_drivers_init(void (*pump_state_changed_cb)(bool is_on), void (*factory_reset_cb)(void));

/**
 * @brief Set the pump state (On or Off).
 * 
 * @param is_on True to turn the pump ON, false to turn it OFF
 */
void gpio_set_pump_state(bool is_on);

/**
 * @brief Get the current state of the pump.
 * 
 * @return true if the pump is ON, false if it is OFF
 */
bool gpio_get_pump_state(void);

/**
 * @brief Set the active external switch mode.
 * 
 * @param mode GH_SWITCH_MODE_PRESS or GH_SWITCH_MODE_HOLD
 */
void gpio_set_switch_mode(gh_switch_mode_t mode);

/**
 * @brief Get the active external switch mode.
 * 
 * @return gh_switch_mode_t Currently configured switch mode
 */
gh_switch_mode_t gpio_get_switch_mode(void);

#ifdef __cplusplus
}
#endif

