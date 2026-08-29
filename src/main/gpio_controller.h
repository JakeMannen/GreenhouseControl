#ifndef GPIO_DRIVERS_H
#define GPIO_DRIVERS_H

#include "esp_err.h"
#include "hal/gpio_types.h"

// Pins and timeouts are now configured via Kconfig / config_manager

/**
 * @brief Initialize the GPIOs for the pump and button.
 * 
 * @param pump_state_changed_cb Callback function to notify other modules (e.g. Zigbee) of a pump state change.
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t gpio_drivers_init(void (*pump_state_changed_cb)(bool is_on));

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

#endif // GPIO_DRIVERS_H
