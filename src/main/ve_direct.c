#include "ve_direct.h"
#include "esp_log.h"
#include "gpio_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "VE_DIRECT";

static SemaphoreHandle_t s_ve_mutex = NULL;
static ve_direct_data_t s_ve_data = {0};
static energy_report_callback_t s_energy_report_cb = NULL;

/**
 * @brief Parses a single line from the VE.Direct text protocol.
 * Maps the string label to the corresponding integer value in the temporary data structure.
 */
void ve_direct_parse_line(const char *label, const char *value, ve_direct_data_t *temp_data) {
    if (strcmp(label, BATTERY_VOLTAGE) == 0) {
        temp_data->battery_voltage_mv = atoi(value);
    } else if (strcmp(label, BATTERY_CURRENT) == 0) {
        temp_data->charge_current_ma = atoi(value);
    } else if (strcmp(label, PANEL_VOLTAGE) == 0) {
        temp_data->panel_voltage_mv = atoi(value);
    } else if (strcmp(label, PANEL_POWER) == 0) {
        temp_data->panel_power_w = atoi(value);
    } else if (strcmp(label, STATE_OF_OPERATION) == 0) {
        temp_data->charge_state = atoi(value);
    } else if (strcmp(label, LOAD_OUTPUT_STATE) == 0) {
        temp_data->load_output_state = (strcmp(value, "ON") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
    } else if (strcmp(label, LOAD_CURRENT) == 0) {
        temp_data->load_current = atoi(value);
    } else if (strcmp(label, OFF_REASON) == 0) {
        temp_data->off_reason = atoi(value);
    } else if (strcmp(label, ERROR_CODE) == 0) {
        temp_data->error_code = atoi(value);
    }
}

/**
 * @brief Finalizes a VE.Direct data block after the "Checksum" field is received.
 * Checks validity, copies data to the global state, and triggers the callback.
 */
esp_err_t ve_direct_finalize_block(ve_direct_data_t *temp_data, uint8_t checksum) {
    if (checksum == 0) {
        if (s_ve_mutex != NULL && xSemaphoreTake(s_ve_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

            s_ve_data = *temp_data;
            s_ve_data.is_updated = true;
            if (s_energy_report_cb != NULL) {
                s_energy_report_cb(&s_ve_data);
            }
            xSemaphoreGive(s_ve_mutex);
            
            ESP_LOGD(TAG, "Parsed VALID VE.Direct block\n\t| Battery voltage: %d mV |\n\t | Battery current: %d mA |\n\t | Panel voltage: %d mV |\n\t | Panel power: %d W |\n\t | Load current: %d mA |\n\t | Load output state: %d |\n\t | Off reason: %d |",
                     s_ve_data.battery_voltage_mv, s_ve_data.charge_current_ma, s_ve_data.panel_voltage_mv, s_ve_data.panel_power_w, s_ve_data.load_current, s_ve_data.load_output_state, s_ve_data.off_reason);
        }
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "VE.Direct Checksum FAILED! (Sum: 0x%02X). Discarding block.", checksum);
        return ESP_ERR_INVALID_CRC;
    }
}

typedef enum {
    VE_STATE_IDLE,
    VE_STATE_RECORD_BEGIN,
    VE_STATE_RECORD_NAME,
    VE_STATE_RECORD_VALUE,
    VE_STATE_CHECKSUM
} ve_parser_state_t;

static ve_parser_state_t s_parser_state = VE_STATE_IDLE;
static uint8_t s_block_checksum = 0;
static char s_name_buf[32];
static size_t s_name_idx = 0;
static char s_value_buf[32];
static size_t s_value_idx = 0;
static ve_direct_data_t s_temp_data = {0};

void ve_direct_reset_parser(void) {
    s_parser_state = VE_STATE_IDLE;
    s_block_checksum = 0;
    s_name_idx = 0;
    s_value_idx = 0;
    memset(&s_temp_data, 0, sizeof(s_temp_data));
}

bool ve_direct_feed_byte(uint8_t c) {
    bool block_finalized = false;

    // Accumulate checksum for every byte in an active block
    if (s_parser_state != VE_STATE_IDLE) {
        s_block_checksum += c;
    }

    switch (s_parser_state) {
        case VE_STATE_IDLE:
            if (c == '\r') {
                s_block_checksum = c;
            } else if (c == '\n' && s_block_checksum == '\r') {
                s_block_checksum += c;
                s_parser_state = VE_STATE_RECORD_BEGIN;
            } else {
                s_block_checksum = 0;
            }
            break;

        case VE_STATE_RECORD_BEGIN:
            if (c == '\r') {
                // Wait for subsequent LF
                break;
            } else if (c == '\n') {
                // Ignore duplicate LF
                break;
            } else if (c == ':') {
                // Victron HEX record start, ignore until text frame restarts
                s_parser_state = VE_STATE_IDLE;
                s_block_checksum = 0;
            } else {
                s_name_buf[0] = (char)c;
                s_name_idx = 1;
                s_parser_state = VE_STATE_RECORD_NAME;
            }
            break;

        case VE_STATE_RECORD_NAME:
            if (c == '\t') {
                s_name_buf[s_name_idx] = '\0';
                if (strcasecmp(s_name_buf, "Checksum") == 0) {
                    s_parser_state = VE_STATE_CHECKSUM;
                } else {
                    s_value_idx = 0;
                    s_parser_state = VE_STATE_RECORD_VALUE;
                }
            } else if (c == '\r' || c == '\n') {
                // Malformed record missing tab delimiter
                s_parser_state = VE_STATE_IDLE;
                s_block_checksum = 0;
            } else if (s_name_idx < sizeof(s_name_buf) - 1) {
                s_name_buf[s_name_idx++] = (char)c;
            }
            break;

        case VE_STATE_RECORD_VALUE:
            if (c == '\r' || c == '\n') {
                s_value_buf[s_value_idx] = '\0';
                ve_direct_parse_line(s_name_buf, s_value_buf, &s_temp_data);
                s_parser_state = VE_STATE_RECORD_BEGIN;
            } else if (s_value_idx < sizeof(s_value_buf) - 1) {
                s_value_buf[s_value_idx++] = (char)c;
            }
            break;

        case VE_STATE_CHECKSUM:
            // Checksum byte has been added to s_block_checksum.
            // In the VE.Direct protocol, a block sum modulo 256 equals 0.
            if (s_block_checksum == 0) {
                ve_direct_finalize_block(&s_temp_data, 0);
                block_finalized = true;
            } else {
                ESP_LOGW(TAG, "VE.Direct Checksum FAILED! (Sum: 0x%02X). Discarding block.", s_block_checksum);
            }
            memset(&s_temp_data, 0, sizeof(s_temp_data));
            s_block_checksum = 0;
            s_name_idx = 0;
            s_value_idx = 0;
            s_parser_state = VE_STATE_IDLE;
            break;
    }

    return block_finalized;
}

/**
 * @brief FreeRTOS task that continuously reads UART data and feeds it into the parser.
 */
static void ve_direct_parser_task(void *pvParameters) {
    uint8_t rx_buf[64];
    
    ESP_LOGI(TAG, "VE.Direct parser task started on UART port %d", VE_DIRECT_UART_PORT);

    while (1) {
        int rx_len = uart_read_bytes(VE_DIRECT_UART_PORT, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (rx_len > 0) {
            for (int i = 0; i < rx_len; i++) {
                ve_direct_feed_byte(rx_buf[i]);
            }
        }
    }
}

esp_err_t ve_direct_init(uart_port_t port, gpio_num_t rx_pin, energy_report_callback_t energy_cb) {

    s_energy_report_cb = energy_cb;

    uart_config_t uart_config = {
        .baud_rate = VE_DIRECT_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(port, VE_DIRECT_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(port, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(port, UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        return err;
    }

    // Enable internal pull-up on RX pin to support open-collector optocouplers (e.g. 4N25)
    gpio_pullup_en(rx_pin);

#if CONFIG_GH_VE_DIRECT_INVERT_RX
    ESP_ERROR_CHECK(uart_set_line_inverse(port, UART_SIGNAL_RXD_INV));
#endif

    s_ve_mutex = xSemaphoreCreateMutex();
    if (s_ve_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create VE.Direct mutex");
        return ESP_ERR_NO_MEM;
    }

    ve_direct_reset_parser();

    xTaskCreate(ve_direct_parser_task, "ve_direct_parser", 3072, NULL, 8, NULL);
    ESP_LOGI(TAG, "VE.Direct initialized on RX pin %d", rx_pin);
    return ESP_OK;
}

bool ve_direct_get_data(ve_direct_data_t *data) {
    if (data == NULL || s_ve_mutex == NULL) {
        return false;
    }

    bool updated = false;
    if (xSemaphoreTake(s_ve_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (s_ve_data.is_updated) {
            data->battery_voltage_mv = s_ve_data.battery_voltage_mv;
            data->charge_current_ma = s_ve_data.charge_current_ma;
            data->panel_voltage_mv = s_ve_data.panel_voltage_mv;
            data->panel_power_w = s_ve_data.panel_power_w;
            data->charge_state = s_ve_data.charge_state;
            data->load_output_state = s_ve_data.load_output_state;
            data->load_current = s_ve_data.load_current;
            data->off_reason = s_ve_data.off_reason;
            data->error_code = s_ve_data.error_code;
            data->is_updated = false;
            
            s_ve_data.is_updated = false; // Reset read flag
            updated = true;
        }
        xSemaphoreGive(s_ve_mutex);
    }
    return updated;
}
