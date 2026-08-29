#include "sht30_sensor.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT30";

static i2c_master_dev_handle_t s_sht30_dev_handle = NULL;
static climate_report_callback_t s_climate_report_cb = NULL;

static int16_t s_temperature_value = -25000;
static int16_t s_humidity_value = -20000;

// CRC-8 calculation with polynomial 0x31 (x^8 + x^5 + x^4 + 1), Initial value: 0xFF
uint8_t sht30_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

esp_err_t sht30_read_temp_humi(gh_climate_data_t *climate_data) {

    if (s_sht30_dev_handle == NULL) {
        ESP_LOGE(TAG, "Sensor not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    // High repeatability (0x240B)
    uint8_t cmd[2] = {0x24, 0x0B};
    
    // 1. Write measure command to sensor
    esp_err_t err = i2c_master_transmit(s_sht30_dev_handle, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send command to sensor: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Wait for measurement to be ready (High repeatability max 15ms)
    vTaskDelay(pdMS_TO_TICKS(20));

    // 3. Read 6 bytes
    // Format: [Temp MSB][Temp LSB][Temp CRC][Humi MSB][Humi LSB][Humi CRC]
    uint8_t data[6] = {0};
    err = i2c_master_receive(s_sht30_dev_handle, data, sizeof(data), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data from SHT30: %s", esp_err_to_name(err));
        return err;
    }

    // 4. Verify CRC for Temperature (index 0 and 1, index 2 is CRC)
    if (sht30_crc8(&data[0], 2) != data[2]) {
        ESP_LOGE(TAG, "Temperature CRC-error!");
        return ESP_ERR_INVALID_CRC;
    }

    // 5. Verify CRC for Humidity (index 3 and 4, index 5 is CRC)
    if (sht30_crc8(&data[3], 2) != data[5]) {
        ESP_LOGE(TAG, "Humidity CRC-error!");
        return ESP_ERR_INVALID_CRC;
    }

    // 6. Convert rawdata to actual values according to SHT30-datasheet
    uint16_t raw_temp = (data[0] << 8) | data[1];
    uint16_t raw_humi = (data[3] << 8) | data[4];

    s_temperature_value = (int16_t)(-4500 + ((17500UL * raw_temp) / 65535UL));
    s_humidity_value = (int16_t)((10000UL * raw_humi) / 65535UL);

    ESP_LOGD(TAG, "Read values - Temp: %.2f C, Humi: %.2f %%", (float)s_temperature_value/100.0f, (float)s_humidity_value/100.0f);

    climate_data->previous_temperature = climate_data->temperature;
    climate_data->previous_humidity = climate_data->humidity;
    climate_data->temperature = s_temperature_value;
    climate_data->humidity = s_humidity_value;
    return ESP_OK;
}

static void sht30_read_task(void *arg) {
    gh_climate_data_t climate_data = {0};

    while (true) {
        esp_err_t err = sht30_read_temp_humi(&climate_data);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read data from SHT30: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(CONFIG_GH_SHT30_POLL_INTERVAL_MS));
            continue;
        }

        if (s_climate_report_cb != NULL) {
            s_climate_report_cb(&climate_data);
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_GH_SHT30_POLL_INTERVAL_MS));
    }
}

esp_err_t sht30_init(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, climate_report_callback_t report_cb) {

    s_climate_report_cb = report_cb;

    // 1. Configure and initialize I2C-bus (Master Bus)
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = port,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Configure and add SHT30-sensor to bus
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT30_I2C_ADDR, // 0x44
        .scl_speed_hz = SHT30_I2C_SPEED_HZ,
    };

    err = i2c_master_bus_add_device(bus_handle, &dev_config, &s_sht30_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SHT30 to I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    xTaskCreate(sht30_read_task, "sht30_read_task", 3072, NULL, 8, NULL);
    ESP_LOGI(TAG, "SHT30 initialized successfully on port %d (SDA:%d, SCL:%d)", port, sda_pin, scl_pin);
    return ESP_OK;
}