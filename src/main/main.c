#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <math.h>

#include "sht30_sensor.h"
#include "gpio_controller.h"
#include "ve_direct.h"
#include "zigbee_controller.h"
#include "led.h"
#include "config_manager.h"

static const char *TAG = "GREENHOUSE_MAIN";

typedef enum {
    EVENT_TYPE_CLIMATE,
    EVENT_TYPE_ENERGY,
    EVENT_TYPE_PUMP
} event_type_t;

typedef struct {
    event_type_t type;
    union {
        gh_climate_data_t climate;
        ve_direct_data_t energy;
        bool pump_is_on;
    } data;
} app_event_t;

static QueueHandle_t s_app_event_queue = NULL;

/**
 * @brief Callback triggered when the SHT30 sensor reports new climate data.
 * Posts the climate event to the central event queue if Zigbee is connected.
 */
static void climate_change_handler(gh_climate_data_t *climate_data){
    if (!zigbee_is_connected()) {
        return;
    }

    app_event_t evt = {
        .type = EVENT_TYPE_CLIMATE,
        .data.climate = *climate_data
    };
    xQueueSend(s_app_event_queue, &evt, 0);
}

/**
 * @brief Callback triggered when the VE.Direct parser receives new energy data.
 * Applies rate limiting and value change thresholds before posting to the event queue.
 */
static void energy_change_handler(ve_direct_data_t *energy_data){
    static ve_direct_data_t last_reported = {0};
    static int64_t last_report_time = 0;
    int64_t now = esp_timer_get_time();
    
    if (!zigbee_is_connected()) {
        return;
    }

    bool should_report = false;
    
    const app_config_t* cfg = config_manager_get();

    // Value threshold: 100mV battery, 500mV solar, 100mA current, 2W power, or load changes
    if (abs(energy_data->battery_voltage_mv - last_reported.battery_voltage_mv) >= cfg->report_threshold_battery_mv) should_report = true;
    if (abs(energy_data->panel_voltage_mv - last_reported.panel_voltage_mv) >= cfg->report_threshold_panel_mv) should_report = true;
    if (abs(energy_data->charge_current_ma - last_reported.charge_current_ma) >= cfg->report_threshold_charge_current_ma) should_report = true;
    if (abs(energy_data->panel_power_w - last_reported.panel_power_w) >= cfg->report_threshold_panel_power_w) should_report = true;
    if (abs(energy_data->load_current - last_reported.load_current) >= cfg->report_threshold_load_current_ma) should_report = true;
    if (energy_data->load_output_state != last_reported.load_output_state) should_report = true;

    // Time threshold: Force report every 5 minutes (300,000,000 us)
    if (now - last_report_time >= cfg->report_interval_max_us) should_report = true;
    
    // For the very first report, always report
    if (last_report_time == 0) should_report = true;

    // Rate limit: Max once per 10 seconds (10,000,000 us)
    if (last_report_time != 0 && now - last_report_time < cfg->report_interval_min_us) should_report = false;

    if (!should_report) {
        return;
    }

    last_reported = *energy_data;
    last_report_time = now;
    
    app_event_t evt = {
        .type = EVENT_TYPE_ENERGY,
        .data.energy = *energy_data
    };
    xQueueSend(s_app_event_queue, &evt, 0);
}

/**
 * @brief Callback triggered when the pump state changes locally (button or safety timer).
 * Reports the new status to the Zigbee network.
 */
static void pump_state_changed_handler(bool is_on) {
    if (!zigbee_is_connected()) {
        ESP_LOGW(TAG, "Pump state changed to %s, but Zigbee is not connected yet.", is_on ? "ON" : "OFF");
        return;
    }
    
    app_event_t evt = {
        .type = EVENT_TYPE_PUMP,
        .data.pump_is_on = is_on
    };
    xQueueSend(s_app_event_queue, &evt, 0);
}

void app_main(void) {

    ESP_LOGI(TAG, "Initializing Greenhouse Controller...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nvs_flash_init_partition("zb_storage");
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase_partition("zb_storage"));
        ret = nvs_flash_init_partition("zb_storage");
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_ERROR_CHECK(config_manager_init());

    s_app_event_queue = xQueueCreate(20, sizeof(app_event_t));
    if (s_app_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create app event queue");
        return;
    }

    ESP_LOGI(TAG, "Entering main application loop");

    ESP_ERROR_CHECK(zigbee_controller_init()); // Start Zigbee in its own task
    light_driver_init(LIGHT_DEFAULT_OFF); // Initialize LED controller
    ESP_ERROR_CHECK(gpio_drivers_init(pump_state_changed_handler)); // Initialize GPIO drivers
    ESP_ERROR_CHECK(sht30_init(SHT30_I2C_PORT, CONFIG_GH_SHT30_SDA_PIN, CONFIG_GH_SHT30_SCL_PIN, climate_change_handler));
    ESP_ERROR_CHECK(ve_direct_init(VE_DIRECT_UART_PORT, CONFIG_GH_VE_DIRECT_RX_PIN, energy_change_handler));
    
    app_event_t evt;
    const app_config_t* cfg = config_manager_get();
    while (true) {
        if (xQueueReceive(s_app_event_queue, &evt, portMAX_DELAY)) {
            switch (evt.type) {
                case EVENT_TYPE_CLIMATE:
                    ESP_LOGI(TAG, "Reporting new climate data");
                    esp_zigbee_lock_acquire(portMAX_DELAY);
                    ezb_zcl_set_attr_value(ZB_CLIMATE_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
                                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
                                           EZB_ZCL_STD_MANUF_CODE, (uint16_t *)&evt.data.climate.temperature, false);
                    zigbee_report_attribute(ZB_CLIMATE_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT, EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID);
                    
                    ezb_zcl_set_attr_value(ZB_CLIMATE_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_ID,
                                           EZB_ZCL_STD_MANUF_CODE, (uint16_t *)&evt.data.climate.humidity, false);                       
                    zigbee_report_attribute(ZB_CLIMATE_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, EZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_ID);
                    esp_zigbee_lock_release();
                    break;

                case EVENT_TYPE_ENERGY:
                    ESP_LOGI(TAG, "Reporting new energy data: Battery %d mV, Solar %d mV, %d mA, %d W, Load %d mA (%s)", 
                             evt.data.energy.battery_voltage_mv, evt.data.energy.panel_voltage_mv, 
                             evt.data.energy.charge_current_ma, evt.data.energy.panel_power_w,
                             evt.data.energy.load_current, evt.data.energy.load_output_state ? "ON" : "OFF");

                    uint8_t batt_val = (uint8_t)(evt.data.energy.battery_voltage_mv / 100);
                    int16_t sol_p = (int16_t)(evt.data.energy.panel_power_w * 10);

                    // Approximate LiFePO4 capacity curve.
                    // This maps millivolts to an estimated percentage since LiFePO4 discharge curves are non-linear.
                    int32_t mv = evt.data.energy.battery_voltage_mv;
                    uint8_t percentage;
                    if (mv >= cfg->batt_curve_100_mv) { percentage = 100; }
                    else if (mv >= cfg->batt_curve_90_mv) { percentage = 90 + ((mv - cfg->batt_curve_90_mv) * 10 / (cfg->batt_curve_100_mv - cfg->batt_curve_90_mv)); }
                    else if (mv >= cfg->batt_curve_70_mv) { percentage = 70 + ((mv - cfg->batt_curve_70_mv) * 20 / (cfg->batt_curve_90_mv - cfg->batt_curve_70_mv)); }
                    else if (mv >= cfg->batt_curve_40_mv) { percentage = 40 + ((mv - cfg->batt_curve_40_mv) * 30 / (cfg->batt_curve_70_mv - cfg->batt_curve_40_mv)); }
                    else if (mv >= cfg->batt_curve_20_mv) { percentage = 20 + ((mv - cfg->batt_curve_20_mv) * 20 / (cfg->batt_curve_40_mv - cfg->batt_curve_20_mv)); }
                    else if (mv >= cfg->batt_curve_0_mv) { percentage = (mv - cfg->batt_curve_0_mv) * 20 / (cfg->batt_curve_20_mv - cfg->batt_curve_0_mv); }
                    else { percentage = 0; }
                    
                    // Zigbee batteryPercentageRemaining is in 0.5% units (200 = 100%)
                    uint8_t batt_pct_zcl = (uint8_t)(percentage * 2);

                    esp_zigbee_lock_acquire(portMAX_DELAY);
                    ezb_zcl_set_attr_value(ZB_PUMP_1_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
                                           EZB_ZCL_STD_MANUF_CODE, &batt_val, false);
                    zigbee_report_attribute(ZB_PUMP_1_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_POWER_CONFIG, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID);
                    
                    ezb_zcl_set_attr_value(ZB_PUMP_1_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                                           EZB_ZCL_CLUSTER_SERVER, 0x0021, // BatteryPercentageRemaining
                                           EZB_ZCL_STD_MANUF_CODE, &batt_pct_zcl, false);
                    zigbee_report_attribute(ZB_PUMP_1_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_POWER_CONFIG, 0x0021);
                    
                    uint16_t sol_v_u16 = (uint16_t)(evt.data.energy.panel_voltage_mv / 10);
                    uint16_t chg_i_u16 = (uint16_t)(evt.data.energy.charge_current_ma / 10);
                    
                    ezb_zcl_set_attr_value(ZB_SOLAR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
                                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_ID,
                                           EZB_ZCL_STD_MANUF_CODE, &sol_v_u16, false);
                    zigbee_report_attribute(ZB_SOLAR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_ID);

                    ezb_zcl_set_attr_value(ZB_SOLAR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
                                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_CURRENT_ID,
                                           EZB_ZCL_STD_MANUF_CODE, &chg_i_u16, false);
                    zigbee_report_attribute(ZB_SOLAR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_CURRENT_ID);

                    ezb_zcl_set_attr_value(ZB_SOLAR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
                                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_ID,
                                           EZB_ZCL_STD_MANUF_CODE, &sol_p, false);
                    zigbee_report_attribute(ZB_SOLAR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_ID);

                    bool load_on = (evt.data.energy.load_output_state != 0);
                    uint16_t load_i_u16 = (uint16_t)(evt.data.energy.load_current / 10);

                    ezb_zcl_set_attr_value(ZB_LOAD_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ON_OFF,
                                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                                           EZB_ZCL_STD_MANUF_CODE, &load_on, false);
                    zigbee_report_attribute(ZB_LOAD_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);

                    ezb_zcl_set_attr_value(ZB_LOAD_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
                                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_CURRENT_ID,
                                           EZB_ZCL_STD_MANUF_CODE, &load_i_u16, false);
                    zigbee_report_attribute(ZB_LOAD_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_CURRENT_ID);
                    esp_zigbee_lock_release();
                    break;

                case EVENT_TYPE_PUMP:
                    ESP_LOGI(TAG, "Reporting pump state over Zigbee: %s", evt.data.pump_is_on ? "ON" : "OFF");
                    esp_zigbee_lock_acquire(portMAX_DELAY);
                    ezb_zcl_set_attr_value(ZB_PUMP_1_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ON_OFF,
                                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                                           EZB_ZCL_STD_MANUF_CODE, &evt.data.pump_is_on, false);
                    zigbee_report_attribute(ZB_PUMP_1_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
                    esp_zigbee_lock_release();
                    break;
            }
        }
    }
}
