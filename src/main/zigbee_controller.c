#include "zigbee_controller.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "ezbee/zcl.h"
#include "led.h"

static const char *TAG = "ZB_CONTROLLER";
static bool g_connected = false;
static esp_timer_handle_t steering_timer;

bool zigbee_is_connected(void)
{
    return g_connected;
}

static void steering_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Retrying network steering");
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
}

static bool zb_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);

    switch (signal_type) {
        case EZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(TAG, "Initialize Zigbee stack");
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
            break;

        case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
            ezb_bdb_comm_status_t status =
                *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
            if (status == EZB_BDB_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Device started in%s factory-reset mode",
                        ezb_bdb_is_factory_new() ? "" : " non");
                if (ezb_bdb_is_factory_new()) {
                    light_driver_set_connecting(); // Indicate network join in progress
                    ESP_LOGI(TAG, "Searching for an existing network to join");
                    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
                } else {
                    light_driver_set_ok(2000); // Indicate successful rejoin
                    ESP_LOGI(TAG, "Re-joined existing network");
                    g_connected = true;
                }
            } else {
                ESP_LOGW(TAG, "%s failed (status: 0x%02x)",
                        ezb_app_signal_to_string(signal_type), status);
                g_connected = false;
                esp_timer_start_once(steering_timer, 2000000); // Retry after 2 seconds
            }
        } break;

        case EZB_BDB_SIGNAL_STEERING: {
            ezb_bdb_comm_status_t status =
                *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
            if (status == EZB_BDB_STATUS_SUCCESS) {
                ezb_extpanid_t ext_panid;
                ezb_nwk_get_extended_panid(&ext_panid);
                ESP_LOGI(TAG,
                        "Joined network: PAN 0x%04hx, EXT 0x%llx, channel %d, short 0x%04hx",
                        ezb_nwk_get_panid(), ext_panid.u64,
                        ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
                light_driver_set_success(); // Indicate successful network join
                g_connected = true;
            } else {
                light_driver_set_error(); // Indicate network join failure
                ESP_LOGW(TAG, "Network steering failed (status: 0x%02x)", status);
                g_connected = false;
                esp_timer_start_once(steering_timer, 2000000); // Retry after 2 seconds
            }
        } break;

        case EZB_ZDO_SIGNAL_DEVICE_ANNCE: {
            const ezb_zdo_signal_device_annce_params_t *p =
                ezb_app_signal_get_params(app_signal);
            ESP_LOGI(TAG, "Device joined/rejoined (short: 0x%04hx)", p->short_addr);
        } break;

        case EZB_ZDO_SIGNAL_LEAVE: {
            ESP_LOGI(TAG, "Leave network, restarting network steering");
            g_connected = false;
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        } break;

        default:
            ESP_LOGI(TAG, "Zigbee signal: %s (0x%02x)",
                    ezb_app_signal_to_string(signal_type), signal_type);
            break;
        }
    return true;
}

static void zb_zcl_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    switch (callback_id) {
        case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID: {
            ezb_zcl_set_attr_value_message_t *m = message;
            ESP_LOGI(TAG, "ZCL SetAttribute ep=%d cluster=0x%04x status=0x%02x",
                    m->info.dst_ep, m->info.cluster_id, m->info.status);
        } break;

        case EZB_ZCL_CORE_DEFAULT_RSP_CB_ID: {
            ezb_zcl_cmd_default_rsp_message_t *m = message;
            ESP_LOGI(TAG, "ZCL Default Response from ep=%d for command=0x%02x status=0x%02x", 
                     m->in.header->src_ep, m->in.rsp_to_cmd, m->in.status_code);
        } break;

        default:
            ESP_LOGI(TAG, "ZCL action: 0x%04lx", (unsigned long)callback_id);
            break;
        }
}

static esp_err_t add_basic_cluster_to_endpoint(ezb_af_ep_desc_t *endpoint_desc) {

    ezb_zcl_cluster_desc_t basic = ezb_af_endpoint_get_cluster_desc(endpoint_desc, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);

    ESP_RETURN_ON_FALSE(basic == NULL, ESP_FAIL, TAG, "endpoint already contains basic cluster");

    // Format manufacturer name as a Zigbee string (length byte followed by characters)
    static uint8_t manuf_p_string[sizeof(ZB_MANUFACTURER_NAME)];
    manuf_p_string[0] = sizeof(ZB_MANUFACTURER_NAME) - 1; 
    memcpy(&manuf_p_string[1], ZB_MANUFACTURER_NAME, manuf_p_string[0]);

    // Format model identifier as a Zigbee string (length byte followed by characters)
    static uint8_t model_p_string[sizeof(ZB_MODEL_IDENTIFIER)];
    model_p_string[0] = sizeof(ZB_MODEL_IDENTIFIER) - 1; 
    memcpy(&model_p_string[1], ZB_MODEL_IDENTIFIER, model_p_string[0]);

    static uint8_t model_hw_version = (uint8_t)ZB_MODEL_HW_VERSION;
    
    // If there is no Basic cluster on this endpoint yet, create it and add it to the endpoint.
     ezb_zcl_basic_cluster_server_config_t basic_cfg = {
            .zcl_version = 0x03,  // Zigbee 3.0 standard
            .power_source = ZB_POWER_SOURCE, // 0x01 = Mains, 0x03 = Battery
        };

    basic = ezb_zcl_basic_create_cluster_desc(&basic_cfg, EZB_ZCL_CLUSTER_SERVER);

    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(endpoint_desc, basic), TAG, "add basic cluster failed");
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,(void *) manuf_p_string);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,(void *) model_p_string);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_HW_VERSION_ID,(void *) &model_hw_version);
    return ESP_OK;

}

static esp_err_t zb_register_climate_endpoint(ezb_af_device_desc_t device_desc)
{
    // Create the Temperature & Humidity Measurement cluster configuration
    static ezb_zcl_temperature_measurement_cluster_server_config_t temperature_meas_cfg = {
        .measured_value = 0,
        .min_measured_value = -4000, // Representing -40.00°C with a resolution of 0.01°C
        .max_measured_value = 12500, // Representing 125.00°C with a resolution of 0.01°C
    };

    static ezb_zcl_rel_humidity_measurement_cluster_server_config_t humidity_meas_cfg = {
        .measured_value = 0,
        .min_measured_value = 0,
        .max_measured_value = 10000, // Representing 0% to 100% with a resolution of 0.01%
    };

    // Create cluster descriptors for Temperature Measurement and Relative Humidity Measurement
    ezb_zcl_cluster_desc_t humidity_cluster = ezb_zcl_rel_humidity_measurement_create_cluster_desc(&humidity_meas_cfg, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_cluster_desc_t temperature_cluster = ezb_zcl_temperature_measurement_create_cluster_desc(&temperature_meas_cfg, EZB_ZCL_CLUSTER_SERVER);

    // Create the Climate endpoint configuration
    ezb_af_ep_config_t climate_ep_config = {
        .ep_id              = ZB_CLIMATE_ENDPOINT_ID,
        .app_profile_id     = EZB_AF_HA_PROFILE_ID,
    };

    // Create the Climate endpoint
    ezb_af_ep_desc_t climate_ep_desc = ezb_af_create_endpoint_desc(&climate_ep_config);

    // Add clusters to the endpoint
    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(climate_ep_desc, humidity_cluster), TAG, "add humidity cluster failed");
    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(climate_ep_desc, temperature_cluster), TAG, "add temperature cluster failed");


    ESP_RETURN_ON_ERROR(ezb_af_device_add_endpoint_desc(device_desc, climate_ep_desc), TAG, "add temperature/humidity endpoint failed");
    return ESP_OK;

}

static esp_err_t zb_register_pump_endpoint(ezb_af_device_desc_t device_desc)
{

    // Create the Pump control endpoint configuration
    ezb_af_ep_config_t pump_ep_config = {
        .ep_id              = ZB_PUMP_1_ENDPOINT_ID,
        .app_profile_id     = EZB_AF_HA_PROFILE_ID,
        .app_device_id = 0x0303
    };

    // Create the Pump endpoint
    ezb_af_ep_desc_t pump_ep_desc = ezb_af_create_endpoint_desc(&pump_ep_config);

    // Create pump on/off cluster configuration
    static ezb_zcl_on_off_cluster_config_t onoff_cfg = {
        .on_off = false,
    };

    // Create pump on/off cluster description
    ezb_zcl_cluster_desc_t on_off_cluster = ezb_zcl_on_off_create_cluster_desc(&onoff_cfg, EZB_ZCL_CLUSTER_SERVER);

    // Add pump on/off cluster description to pump endpoint
    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(pump_ep_desc, on_off_cluster), TAG, "add pump 1 on/off cluster failed");

    // Add power config cluster to report battery voltage and percentage
    ezb_zcl_cluster_desc_t power_cluster = ezb_zcl_power_config_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    static uint8_t battery_voltage = 0xFF;
    static uint8_t battery_percentage = 0xFF; // Unknown percentage by default
    
    ezb_zcl_power_config_cluster_desc_add_attr(power_cluster, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &battery_voltage);
    // Force battery voltage to be reportable
    ezb_zcl_attr_desc_t batt_v_desc = ezb_zcl_cluster_get_attr_desc(power_cluster, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, EZB_ZCL_STD_MANUF_CODE);
    if (batt_v_desc) {
        ezb_zcl_attr_desc_set_access(batt_v_desc, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_REPORTING);
    }
    
    // Z2M attempts to configure reporting for batteryPercentageRemaining (0x0021) by default.
    // We add it here (even if we don't actively update it) to prevent UNSUPPORTED_ATTRIBUTE errors.
    ezb_zcl_power_config_cluster_desc_add_attr(power_cluster, 0x0021, &battery_percentage);
    // Force battery percentage to be reportable
    ezb_zcl_attr_desc_t batt_p_desc = ezb_zcl_cluster_get_attr_desc(power_cluster, 0x0021, EZB_ZCL_STD_MANUF_CODE);
    if (batt_p_desc) {
        ezb_zcl_attr_desc_set_access(batt_p_desc, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_REPORTING);
    }
    
    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(pump_ep_desc, power_cluster), TAG, "add power config cluster failed");

    ESP_ERROR_CHECK(add_basic_cluster_to_endpoint(pump_ep_desc));

    ESP_RETURN_ON_ERROR(ezb_af_device_add_endpoint_desc(device_desc, pump_ep_desc), TAG, "add pump endpoint failed");
    return ESP_OK;

}

static esp_err_t zb_register_solar_endpoint(ezb_af_device_desc_t device_desc)
{
    // Create the Solar endpoint configuration
    ezb_af_ep_config_t solar_ep_config = {
        .ep_id              = ZB_SOLAR_ENDPOINT_ID,
        .app_profile_id     = EZB_AF_HA_PROFILE_ID,
    };

    // Create the Solar endpoint
    ezb_af_ep_desc_t solar_ep_desc = ezb_af_create_endpoint_desc(&solar_ep_config);

    // Create Electrical Measurement cluster
    ezb_zcl_cluster_desc_t em_cluster = ezb_zcl_electrical_measurement_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);

    static int16_t default_s16 = 0;
    static uint16_t default_u16 = 0;
    static uint32_t default_type = EZB_ZCL_ELECTRICAL_MEASUREMENT_MEASUREMENT_TYPE_DC_MEASUREMENT;
    
    // Divisors and multipliers (uint16_t)
    static uint16_t mult_1 = 1;
    static uint16_t div_10 = 10;
    static uint16_t div_100 = 100;
    
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_MEASUREMENT_TYPE_ID, &default_type);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_ID, &default_u16);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_CURRENT_ID, &default_u16);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_ID, &default_s16);

    // Multipliers and divisors to keep Z2M happy and properly scale values
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_AC_VOLTAGE_MULTIPLIER_ID, &mult_1);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_AC_VOLTAGE_DIVISOR_ID, &div_100);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_AC_CURRENT_MULTIPLIER_ID, &mult_1);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_AC_CURRENT_DIVISOR_ID, &div_100);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_AC_POWER_MULTIPLIER_ID, &mult_1);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_AC_POWER_DIVISOR_ID, &div_10);

    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(solar_ep_desc, em_cluster), TAG, "add electrical measurement cluster failed");

    ESP_RETURN_ON_ERROR(ezb_af_device_add_endpoint_desc(device_desc, solar_ep_desc), TAG, "add solar endpoint failed");
    return ESP_OK;
}

static esp_err_t zb_register_endpoints(void)
{

    // Create the main device
    ezb_af_device_desc_t device_desc = ezb_af_create_device_desc();

    // Create the Pump endpoint, also adds basic cluster to endpoint
    zb_register_pump_endpoint(device_desc);

    // Create the Climate endpoint
    zb_register_climate_endpoint(device_desc);

    // Create the Solar (Electrical Measurement) endpoint
    zb_register_solar_endpoint(device_desc);

    ESP_RETURN_ON_ERROR(ezb_af_device_desc_register(device_desc), TAG, "register device failed");
    ezb_zcl_core_action_handler_register(zb_zcl_action_handler);
    return ESP_OK;
}

static void zb_main_task(void *arg)
{
    esp_zigbee_config_t config = ZB_DEFAULT_CONFIG();

    ESP_ERROR_CHECK(esp_zigbee_init(&config));

    esp_timer_create_args_t timer_args = {
        .callback = steering_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "steering_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &steering_timer));

    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(ZB_PRIMARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(ZB_SECONDARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(zb_app_signal_handler));

    ESP_ERROR_CHECK(zb_register_endpoints());

    ESP_ERROR_CHECK(esp_zigbee_start(false));

    esp_zigbee_launch_mainloop();

    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

void zigbee_report_attribute(uint8_t ep, uint16_t cluster_id, uint16_t attribute_id)
{
    ezb_zcl_report_attr_cmd_t report_attr_cmd = { 0 };
    report_attr_cmd.cmd_ctrl.dst_addr.addr_mode = EZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
    report_attr_cmd.cmd_ctrl.cluster_id = cluster_id;
    report_attr_cmd.payload.attr_id = attribute_id;
    report_attr_cmd.cmd_ctrl.fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI;
    report_attr_cmd.cmd_ctrl.src_ep = ep;
    
    ezb_zcl_report_attr_cmd_req(&report_attr_cmd);
    ESP_LOGD(TAG, "Sent report request for EP:%d, Cluster:0x%04X, Attr:0x%04X", ep, cluster_id, attribute_id);
}

esp_err_t zigbee_controller_init(void)
{
    BaseType_t ok = xTaskCreate(zb_main_task, "zigbee_main",
                                ZB_TASK_STACK, NULL, ZB_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Zigbee task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
