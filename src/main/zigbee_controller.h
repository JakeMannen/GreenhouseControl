#pragma once

#include "esp_err.h"
#include "sht30_sensor.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "ezbee/zcl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_POWER_SOURCE             EZB_ZCL_BASIC_POWER_SOURCE_BATTERY

#define ZB_PUMP_1_ENDPOINT_ID       1
#define ZB_CLIMATE_ENDPOINT_ID      2
#define ZB_SOLAR_ENDPOINT_ID        3
#define ZB_LOAD_ENDPOINT_ID         4

#define ZB_PRIMARY_CHANNEL_MASK    (1U << 13)
#define ZB_SECONDARY_CHANNEL_MASK  (0x07FFF800U)

#define ZB_MANUFACTURER_NAME       CONFIG_GH_ZIGBEE_MANUFACTURER_NAME
#define ZB_MODEL_IDENTIFIER        CONFIG_GH_ZIGBEE_MODEL_IDENTIFIER
#define ZB_MODEL_HW_VERSION        CONFIG_GH_ZIGBEE_HW_VERSION

#ifdef APP_VERSION_STR
#define ZB_SW_BUILD_ID             APP_VERSION_STR
#else
#define ZB_SW_BUILD_ID             CONFIG_GH_ZIGBEE_SW_BUILD_ID
#endif

#ifdef OTA_FILE_VERSION
#define ZB_APP_VERSION             (uint8_t)((OTA_FILE_VERSION >> 24) & 0xFF)
#else
#define ZB_APP_VERSION             CONFIG_GH_ZIGBEE_APP_VERSION
#endif

#define ZB_TASK_STACK              4096
#define ZB_TASK_PRIORITY           5

#define ZB_DEVICE_CONFIG()                              \
    {                                                   \
        .device_type = EZB_NWK_DEVICE_TYPE_ROUTER,      \
        .install_code_policy = false,                   \
        .zczr_config = {                                \
            .max_children = 10,                         \
        },                                              \
    }

#if CONFIG_SOC_IEEE802154_SUPPORTED
#define ZB_PLATFORM_CONFIG()                                 \
    {                                                        \
        .storage_partition_name = "zb_storage",              \
        .radio_config = {                                    \
            .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,      \
        },                                                   \
    }
#else
#error "This scaffold targets SoCs with native IEEE 802.15.4 (ESP32-C6/H2)."
#endif

#define ZB_DEFAULT_CONFIG()                          \
    {                                                \
        .device_config = ZB_DEVICE_CONFIG(),         \
        .platform_config = ZB_PLATFORM_CONFIG(),     \
    }


/**
 * @brief Initialize the Zigbee stack and spawn the Zigbee main-loop task.
 *
 * Must be called once after NVS has been initialized. Returns immediately;
 * all Zigbee work runs in a dedicated FreeRTOS task so the caller is free
 * to run its own main loop.
 * 
 * @return 
 *     - ESP_OK: Success.
 *     - Other error code: Failed.
 */
esp_err_t zigbee_controller_init(void);

/**
 * @brief Report a changed attribute value to the coordinator.
 * 
 * @param ep Endpoint ID.
 * @param cluster_id Cluster ID.
 * @param attribute_id Attribute ID.
 */
void zigbee_report_attribute(uint8_t ep, uint16_t cluster_id, uint16_t attribute_id);

/**
 * @brief Check if the device is currently connected to a Zigbee network.
 * 
 * @return 
 *     - true: Connected.
 *     - false: Not connected.
 */
bool zigbee_is_connected(void);

#ifdef __cplusplus
}
#endif
