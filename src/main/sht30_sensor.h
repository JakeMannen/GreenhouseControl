#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "gh_datatypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHT30_I2C_ADDR         0x44     // SHT30 default I2C address (ADDR pin connected to GND)
#define SHT30_I2C_PORT         I2C_NUM_0
#define SHT30_I2C_SPEED_HZ     100000   // 100kHz I2C clock speed

// Exposed for testing
uint8_t sht30_crc8(const uint8_t *data, size_t len);

/**
 * @brief Initialize the SHT30 sensor and start the polling task.
 * 
 * @param port I2C port number.
 * @param sda_pin GPIO pin for SDA.
 * @param scl_pin GPIO pin for SCL.
 * @param report_cb Callback function to report climate measurements.
 * 
 * @return 
 *     - ESP_OK: Success.
 *     - Other error code: Failed.
 */
esp_err_t sht30_init(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, climate_report_callback_t report_cb);

#ifdef __cplusplus
}
#endif

