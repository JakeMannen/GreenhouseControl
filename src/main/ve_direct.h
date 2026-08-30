#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "gh_datatypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VE_DIRECT_UART_PORT    UART_NUM_1
#define VE_DIRECT_BAUD_RATE    19200
#define VE_DIRECT_BUF_SIZE     1024

#define VE_INVERT_SIGNAL       1

// RX GPIO is configured via Kconfig

#define BATTERY_VOLTAGE        "V"
#define BATTERY_CURRENT        "I"
#define PANEL_VOLTAGE          "VPV"
#define PANEL_POWER            "PPV"
#define LOAD_OUTPUT_STATE      "LOAD"
#define LOAD_CURRENT           "IL"
#define OFF_REASON             "OR"
#define RELAY_STATE            "Relay"
#define ERROR_CODE             "ERR"
#define STATE_OF_OPERATION     "CS"
#define YIELD_TOTAL            "H19"
#define YIELD_TODAY            "H20"
#define MAX_POWER_TODAY        "H21"
#define YIELD_YESTERDAY        "H22"
#define MAX_POWER_YESTERDAY    "H23"
#define PRODUCT_ID             "PID"
#define SERIAL_NUMBER          "SER#"
#define DAY_SEQUENCE           "HSDS"
#define TRACKER_OPERATION_MODE "MPPT"     

typedef struct {
    int32_t battery_voltage_mv;   // V (mV)
    int32_t charge_current_ma;    // I (mA)
    int32_t panel_voltage_mv;     // VPV (mV)
    int32_t panel_power_w;        // PPV (W)
    int32_t charge_state;         // CS (0=Off, 2=Fault, 3=Bulk, 4=Absorption, 5=Float)
    int32_t load_output_state;
    int32_t load_current;
    int32_t off_reason;
    int32_t error_code;
    bool is_updated;              // Flag to indicate new data is available
} ve_direct_data_t;

typedef void (*energy_report_callback_t)(ve_direct_data_t *data);

/**
 * @brief Initialize UART for reading the Victron VE.Direct text stream.
 * 
 * @param port UART port number.
 * @param rx_pin GPIO pin for RX (connected to VE.Direct TX).
 * @param energy_cb Callback function to report data.
 * 
 * @return 
 *     - ESP_OK: Success.
 *     - Other error code: Failed.
 */
esp_err_t ve_direct_init(uart_port_t port, gpio_num_t rx_pin, energy_report_callback_t energy_cb);

/**
 * @brief Retrieve the latest parsed VE.Direct telemetry data.
 * 
 * @param data Pointer to a struct where the telemetry data will be copied.
 * 
 * @return 
 *     - true: New data was retrieved since the last call.
 *     - false: No new data available.
 */
bool ve_direct_get_data(ve_direct_data_t *data);

// --- Exposed for Unit Testing ---
void ve_direct_parse_line(const char *label, const char *value, ve_direct_data_t *temp_data);
esp_err_t ve_direct_finalize_block(ve_direct_data_t *temp_data, uint8_t checksum);

#ifdef __cplusplus
}
#endif

