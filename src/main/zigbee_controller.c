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
#include "ezbee/zcl/cluster/ota_upgrade.h"
#include "ezbee/zcl/cluster/ota_upgrade_desc.h"
#include "ezbee/zcl/cluster/ota_file.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#include "led.h"

static const char *TAG = "ZB_CONTROLLER";
static bool s_is_connected = false;
static esp_timer_handle_t s_steering_timer;

static const esp_partition_t *s_ota_partition = NULL;
static esp_ota_handle_t s_ota_handle = 0;
static bool s_ota_in_progress = false;
static uint32_t s_ota_total_size = 0;
static uint32_t s_ota_received_size = 0;
static uint32_t s_ota_written_size = 0;
static bool s_tag_header_parsed = false;
static uint16_t s_ota_tag_id = 0;
static uint32_t s_ota_tag_len = 0;
static uint16_t s_ota_header_len = 0;

bool zigbee_is_connected(void)
{
    return s_is_connected;
}

static bool s_pairing_mode_active = false;
static esp_timer_handle_t s_pairing_window_timer;
static bool s_factory_reset_scheduled = false;

#ifdef OTA_FILE_VERSION
static uint32_t g_ota_file_version = OTA_FILE_VERSION;
static uint32_t g_ota_downloaded_file_version = OTA_FILE_VERSION;
#endif

static void pairing_window_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Pairing window (3 minutes) expired. Stopping continuous steering.");
    s_pairing_mode_active = false;
    light_driver_set_error(); // Indicate pairing failed
}

static void steering_timer_cb(void *arg)
{
    if (s_pairing_mode_active) {
        ESP_LOGD(TAG, "Retrying network steering in the background");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
    }
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
                    s_pairing_mode_active = true;
                    esp_timer_start_once(s_pairing_window_timer, 180000000); // 3 minutes timeout
                    light_driver_set_connecting(); // Indicate network join in progress
                    ESP_LOGI(TAG, "Searching for an existing network to join (3 min window)");
                    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
                } else {
                    light_driver_set_ok(2000); // Indicate successful rejoin
                    ESP_LOGI(TAG, "Re-joined existing network");
                    s_is_connected = true;
                }
            } else {
                ESP_LOGW(TAG, "%s failed (status: 0x%02x)",
                        ezb_app_signal_to_string(signal_type), status);
                s_is_connected = false;
                if (s_pairing_mode_active) {
                    esp_timer_start_once(s_steering_timer, 1000000); // Silent retry after 1 second
                }
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
                
                if (s_pairing_mode_active) {
                    s_pairing_mode_active = false;
                    esp_timer_stop(s_pairing_window_timer);
                }
                
                light_driver_set_success(); // Indicate successful network join
                s_is_connected = true;
            } else {
                s_is_connected = false;
                if (s_pairing_mode_active) {
                    ESP_LOGD(TAG, "Network steering failed (status: 0x%02x), silently retrying...", status);
                    esp_timer_start_once(s_steering_timer, 1000000); // Silent retry after 1 second
                } else {
                    ESP_LOGW(TAG, "Network steering failed (status: 0x%02x) outside pairing window", status);
                    light_driver_set_error(); // Indicate network join failure
                }
            }
        } break;

        case EZB_ZDO_SIGNAL_DEVICE_ANNCE: {
            const ezb_zdo_signal_device_annce_params_t *p =
                ezb_app_signal_get_params(app_signal);
            ESP_LOGI(TAG, "Device joined/rejoined (short: 0x%04hx)", p->short_addr);
        } break;

        case EZB_ZDO_SIGNAL_LEAVE: {
            ESP_LOGI(TAG, "Leave network, restarting network steering");
            s_is_connected = false;
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        } break;

        default:
            ESP_LOGD(TAG, "Zigbee signal: %s (0x%02x)",
                    ezb_app_signal_to_string(signal_type), signal_type);
            break;
        }
    return true;
}

static uint32_t s_image_start_offset = 0;
static uint32_t s_last_percent = 0;

static esp_err_t ota_stream_write_block(uint32_t file_offset, uint8_t *block, uint8_t block_size)
{
    if (!s_ota_in_progress || !block || block_size == 0) {
        return ESP_FAIL;
    }

    // 1. Extract header length from the very first block (offset 6)
    if (s_ota_header_len == 0 && file_offset == 0 && block_size >= 8) {
        s_ota_header_len = *((uint16_t *)(block + 6));
        s_image_start_offset = s_ota_header_len + 6; // 6 bytes for the upgrade image tag header
        ESP_LOGI(TAG, "OTA Header parsed: hdr_len=%lu, image_start_offset=%lu",
                 (unsigned long)s_ota_header_len, (unsigned long)s_image_start_offset);
    }

    // 2. If we haven't found the start offset yet, we can't write (should not happen if first block >= 8 bytes)
    if (s_image_start_offset == 0) {
        return ESP_OK;
    }

    // 3. Write only the portion of the block that falls AFTER the headers
    if (file_offset + block_size > s_image_start_offset) {
        uint32_t payload_offset = file_offset;
        if (payload_offset < s_image_start_offset) {
            payload_offset = s_image_start_offset;
        }
        
        uint32_t expected_offset = s_image_start_offset + s_ota_written_size;
        
        if (payload_offset < expected_offset) {
            ESP_LOGW(TAG, "Duplicate block received at offset %lu (expected %lu), skipping...", 
                     (unsigned long)file_offset, (unsigned long)expected_offset);
            return ESP_OK; // Ignore duplicate block
        }
        if (payload_offset > expected_offset) {
            ESP_LOGE(TAG, "Missing block! Expected offset %lu, got %lu. OTA flash sequence corrupted!", 
                     (unsigned long)expected_offset, (unsigned long)payload_offset);
            return ESP_FAIL; // Abort because sequential write is broken
        }

        uint32_t write_offset_in_block = payload_offset - file_offset;
        uint32_t chunk_remaining = block_size - write_offset_in_block;
        uint8_t *chunk_ptr = block + write_offset_in_block;

        esp_err_t err = esp_ota_write(s_ota_handle, (const void *)chunk_ptr, chunk_remaining);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed (written=%lu, len=%lu): %s",
                     (unsigned long)s_ota_written_size, (unsigned long)chunk_remaining, esp_err_to_name(err));
            return err;
        }
        s_ota_written_size += chunk_remaining;
    }

    return ESP_OK;
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

        case EZB_ZCL_CORE_OTA_UPGRADE_QUERY_NEXT_IMAGE_RSP_CB_ID: {
            ezb_zcl_ota_upgrade_query_next_image_rsp_message_t *m = message;
            if (m) {
                if (m->in.image.status == EZB_ZCL_OTA_UPGRADE_STATUS_CODE_SUCCESS) {
                    ESP_LOGI(TAG, "OTA Query Next Image Response: update available! manuf=0x%04x, type=0x%04x, ver=0x%08lx, size=%lu",
                             m->in.image.manuf_code, m->in.image.image_type,
                             (unsigned long)m->in.image.file_version, (unsigned long)m->in.image.size);
                    m->out.result = EZB_ZCL_STATUS_SUCCESS;
                } else {
                    ESP_LOGI(TAG, "OTA Query Next Image Response: no update (status 0x%02x)", m->in.image.status);
                    m->out.result = EZB_ZCL_STATUS_SUCCESS;
                }
            }
        } break;

        case EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID: {
            ezb_zcl_ota_upgrade_client_progress_message_t *m = message;
            if (!m) break;

            switch (m->in.progress) {
                case EZB_ZCL_OTA_UPGRADE_PROGRESS_START: {
                    ESP_LOGI(TAG, "OTA Upgrade Progress START: manuf=0x%04x, type=0x%04x, ver=0x%08lx, total_size=%lu",
                             m->in.start.manuf_code, m->in.start.image_type,
                             (unsigned long)m->in.start.file_version, (unsigned long)m->in.start.image_size);

                    s_ota_total_size = m->in.start.image_size;
                    s_ota_received_size = 0;
                    s_ota_written_size = 0;
                    s_last_percent = 0;
                    s_tag_header_parsed = false;
                    s_ota_tag_id = 0;
                    s_ota_tag_len = 0;
                    s_ota_header_len = 0;
                    s_image_start_offset = 0;

                    s_ota_partition = esp_ota_get_next_update_partition(NULL);
                    if (!s_ota_partition) {
                        ESP_LOGE(TAG, "No OTA partition found to flash");
                        m->out.result = EZB_ZCL_STATUS_FAIL;
                        break;
                    }
                    ESP_LOGI(TAG, "Writing to OTA partition subtype %d at offset 0x%lx (size=%lu)",
                             s_ota_partition->subtype, (unsigned long)s_ota_partition->address, (unsigned long)s_ota_total_size);

                    // Pre-erase the required image size in flash upfront so packet writes during download are instantaneous
                    esp_err_t err = esp_ota_begin(s_ota_partition, s_ota_total_size > 0 ? s_ota_total_size : OTA_SIZE_UNKNOWN, &s_ota_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
                        s_ota_in_progress = false;
                        m->out.result = EZB_ZCL_STATUS_FAIL;
                        break;
                    }

                    s_ota_in_progress = true;
                    m->out.result = EZB_ZCL_STATUS_SUCCESS;
                } break;

                case EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING: {
                    s_ota_received_size = m->in.receiving.file_offset + m->in.receiving.block_size;
                    uint32_t percent = s_ota_total_size > 0 ? (s_ota_received_size * 100) / s_ota_total_size : 0;
                    if (percent != s_last_percent || percent == 100) {
                        s_last_percent = percent;
                        ESP_LOGI(TAG, "OTA Download Progress: %lu%% (%lu/%lu bytes)",
                                 (unsigned long)percent, (unsigned long)s_ota_received_size, (unsigned long)s_ota_total_size);
                    }

                    esp_err_t err = ota_stream_write_block(m->in.receiving.file_offset, m->in.receiving.block, m->in.receiving.block_size);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed writing OTA block at offset %lu: %s. Aborting OTA session.",
                                 (unsigned long)m->in.receiving.file_offset, esp_err_to_name(err));
                        if (s_ota_handle) {
                            esp_ota_abort(s_ota_handle);
                            s_ota_handle = 0;
                        }
                        s_ota_in_progress = false;
                        m->out.result = EZB_ZCL_STATUS_FAIL;
                    } else {
                        m->out.result = EZB_ZCL_STATUS_SUCCESS;
                    }
                } break;

                case EZB_ZCL_OTA_UPGRADE_PROGRESS_CHECK: {
                    ESP_LOGI(TAG, "OTA Upgrade Progress CHECK: manuf=0x%04x, type=0x%04x, ver=0x%08lx",
                             m->in.check.manuf_code, m->in.check.image_type, (unsigned long)m->in.check.file_version);
                    ESP_LOGI(TAG, "OTA Total received=%lu bytes, written to partition=%lu bytes (tag_len=%lu)",
                             (unsigned long)s_ota_received_size, (unsigned long)s_ota_written_size, (unsigned long)s_ota_tag_len);
                    m->out.result = EZB_ZCL_STATUS_SUCCESS;
                } break;

                case EZB_ZCL_OTA_UPGRADE_PROGRESS_APPLY: {
                    ESP_LOGI(TAG, "OTA Upgrade Progress APPLY: manuf=0x%04x, type=0x%04x, ver=0x%08lx",
                             m->in.apply.manuf_code, m->in.apply.image_type, (unsigned long)m->in.apply.file_version);

                    if (s_ota_in_progress && s_ota_handle) {
                        esp_err_t err = esp_ota_end(s_ota_handle);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
                            m->out.result = EZB_ZCL_STATUS_FAIL;
                            break;
                        }
                        s_ota_handle = 0;

                        err = esp_ota_set_boot_partition(s_ota_partition);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
                            m->out.result = EZB_ZCL_STATUS_FAIL;
                            break;
                        }
                        ESP_LOGI(TAG, "Next boot partition set successfully to subtype %d (address 0x%lx)",
                                 s_ota_partition->subtype, (unsigned long)s_ota_partition->address);
                    }
                    m->out.result = EZB_ZCL_STATUS_SUCCESS;
                } break;

                case EZB_ZCL_OTA_UPGRADE_PROGRESS_FINISH: {
                    ESP_LOGI(TAG, "OTA Upgrade Progress FINISH: countdown delay = %lu seconds. Restarting device...",
                             (unsigned long)m->in.finish.count_down_delay);
                    s_ota_in_progress = false;
                    m->out.result = EZB_ZCL_STATUS_SUCCESS;
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                } break;

                case EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT: {
                    ESP_LOGW(TAG, "OTA Upgrade Progress ABORT");
                    if (s_ota_in_progress && s_ota_handle) {
                        esp_ota_abort(s_ota_handle);
                        s_ota_handle = 0;
                    }
                    s_ota_in_progress = false;
                    m->out.result = EZB_ZCL_STATUS_SUCCESS;
                } break;

                default:
                    ESP_LOGW(TAG, "Unknown OTA progress state: %d", m->in.progress);
                    m->out.result = EZB_ZCL_STATUS_SUCCESS;
                    break;
            }
        } break;

        default:
            ESP_LOGI(TAG, "ZCL action: 0x%04lx", (unsigned long)callback_id);
            break;
    }
}

static bool zb_raw_frame_handler(const ezb_zcl_raw_frame_t *raw_frame)
{
    if (!raw_frame || !raw_frame->header) {
        return false;
    }

    // Intercept OTA ImageNotify command (cluster 0x0019, cmd 0x00)
    if (raw_frame->header->cluster_id == EZB_ZCL_CLUSTER_ID_OTA_UPGRADE &&
        raw_frame->header->cmd_id == EZB_ZCL_CMD_OTA_UPGRADE_IMAGE_NOTIFY_ID) {

        uint8_t payload_type = 0;
        uint8_t query_jitter = 100;
        if (raw_frame->payload_length >= 1) {
            payload_type = raw_frame->payload[0];
        }
        if (raw_frame->payload_length >= 2) {
            query_jitter = raw_frame->payload[1];
        }

        ESP_LOGI(TAG, "Received OTA ImageNotify (payload_type=%u, jitter=%u) from 0x%04x:ep%d",
                 payload_type, query_jitter, raw_frame->header->src_addr.u.short_addr, raw_frame->header->src_ep);

        // Send QueryNextImageRequest with actual firmware version
        ezb_zcl_ota_upgrade_query_next_image_req_cmd_t query_req = {
            .cmd_ctrl = {
                .dst_ep = raw_frame->header->src_ep,
                .src_ep = ZB_PUMP_1_ENDPOINT_ID,
                .dst_addr.addr_mode = EZB_ADDR_MODE_SHORT,
                .dst_addr.u.short_addr = raw_frame->header->src_addr.u.short_addr,
            },
            .payload = {
                .fc = 0,
                .manuf_code = 0x1001,
                .image_type = 0x1011,
#ifdef OTA_FILE_VERSION
                .file_version = OTA_FILE_VERSION,
#else
                .file_version = 0,
#endif
                .hw_version = ZB_MODEL_HW_VERSION,
            },
        };

        esp_err_t ret = ezb_zcl_ota_upgrade_query_next_image_cmd_req(&query_req);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send QueryNextImageRequest: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Sent QueryNextImageRequest to 0x%04x:ep%d",
                     raw_frame->header->src_addr.u.short_addr, raw_frame->header->src_ep);
        }

        // Return true to consume frame so stack doesn't send UNSUP_COMMAND
        return true;
    }

    return false;
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

    // Format date code as a Zigbee string (length byte followed by characters)
    static uint8_t date_code_p_string[sizeof(ZB_DATE_CODE)];
    date_code_p_string[0] = sizeof(ZB_DATE_CODE) - 1;
    memcpy(&date_code_p_string[1], ZB_DATE_CODE, date_code_p_string[0]);

    static uint8_t model_hw_version = (uint8_t)ZB_MODEL_HW_VERSION;
    static uint8_t app_version = (uint8_t)ZB_APP_VERSION;

    // Format software build ID as a Zigbee string (max 16 chars per ZCL spec)
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const char *sw_ver = (app_desc && app_desc->version[0] != '\0') ? app_desc->version : ZB_SW_BUILD_ID;
    size_t sw_len = strlen(sw_ver);
    if (sw_len > 16) {
        sw_len = 16;
    }
    static uint8_t sw_build_p_string[17];
    sw_build_p_string[0] = (uint8_t)sw_len;
    memcpy(&sw_build_p_string[1], sw_ver, sw_len);
    
    // If there is no Basic cluster on this endpoint yet, create it and add it to the endpoint.
    ezb_zcl_basic_cluster_server_config_t basic_cfg = {
        .zcl_version = 0x03,  // Zigbee 3.0 standard
        .power_source = ZB_POWER_SOURCE, // 0x01 = Mains, 0x03 = Battery
    };

    basic = ezb_zcl_basic_create_cluster_desc(&basic_cfg, EZB_ZCL_CLUSTER_SERVER);

    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(endpoint_desc, basic), TAG, "add basic cluster failed");
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)manuf_p_string);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)model_p_string);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_DATE_CODE_ID, (void *)date_code_p_string);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_HW_VERSION_ID, (void *)&model_hw_version);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, (void *)&app_version);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID, (void *)sw_build_p_string);
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

    // Force temperature to be reportable
    ezb_zcl_attr_desc_t temp_desc = ezb_zcl_cluster_get_attr_desc(temperature_cluster, EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID, EZB_ZCL_STD_MANUF_CODE);
    if (temp_desc) {
        ezb_zcl_attr_desc_set_access(temp_desc, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_REPORTING);
    }

    // Force humidity to be reportable
    ezb_zcl_attr_desc_t hum_desc = ezb_zcl_cluster_get_attr_desc(humidity_cluster, EZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_ID, EZB_ZCL_STD_MANUF_CODE);
    if (hum_desc) {
        ezb_zcl_attr_desc_set_access(hum_desc, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_REPORTING);
    }

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

    // Create OTA Upgrade client config
    static ezb_zcl_ota_upgrade_cluster_client_config_t ota_client_config = {
        .upgrade_server_id = 0xFFFFFFFFFFFFFFFF, // default
        .file_offset = 0,
        .image_upgrade_status = 0, // EZB_ZCL_OTA_UPGRADE_STATUS_NORMAL
        .manufacturer_id = 0x1001,
        .image_type_id = 0x1011,
    };
    ezb_zcl_cluster_desc_t ota_cluster = ezb_zcl_ota_upgrade_create_cluster_desc(&ota_client_config, EZB_ZCL_CLUSTER_CLIENT);

#ifdef OTA_FILE_VERSION
    ezb_zcl_attr_desc_t curr_ver_desc = ezb_zcl_cluster_get_attr_desc(ota_cluster, EZB_ZCL_ATTR_OTA_UPGRADE_CURRENT_FILE_VERSION_ID, EZB_ZCL_STD_MANUF_CODE);
    if (curr_ver_desc) {
        ezb_zcl_attr_desc_set_value(curr_ver_desc, &g_ota_file_version);
        ezb_zcl_attr_desc_set_access(curr_ver_desc, EZB_ZCL_ATTR_ACCESS_READ);
    } else {
        curr_ver_desc = ezb_zcl_create_attr_desc(EZB_ZCL_ATTR_OTA_UPGRADE_CURRENT_FILE_VERSION_ID, 
                                                 EZB_ZCL_ATTR_TYPE_UINT32, 
                                                 EZB_ZCL_ATTR_ACCESS_READ, 
                                                 EZB_ZCL_STD_MANUF_CODE, 
                                                 &g_ota_file_version);
        if (curr_ver_desc) {
            ezb_zcl_cluster_add_attr_desc(ota_cluster, curr_ver_desc);
        }
    }

    ezb_zcl_attr_desc_t down_ver_desc = ezb_zcl_cluster_get_attr_desc(ota_cluster, EZB_ZCL_ATTR_OTA_UPGRADE_DOWNLOADED_FILE_VERSION_ID, EZB_ZCL_STD_MANUF_CODE);
    if (down_ver_desc) {
        ezb_zcl_attr_desc_set_value(down_ver_desc, &g_ota_downloaded_file_version);
        ezb_zcl_attr_desc_set_access(down_ver_desc, EZB_ZCL_ATTR_ACCESS_READ);
    } else {
        down_ver_desc = ezb_zcl_create_attr_desc(EZB_ZCL_ATTR_OTA_UPGRADE_DOWNLOADED_FILE_VERSION_ID, 
                                                 EZB_ZCL_ATTR_TYPE_UINT32, 
                                                 EZB_ZCL_ATTR_ACCESS_READ, 
                                                 EZB_ZCL_STD_MANUF_CODE, 
                                                 &g_ota_downloaded_file_version);
        if (down_ver_desc) {
            ezb_zcl_cluster_add_attr_desc(ota_cluster, down_ver_desc);
        }
    }
#endif

    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(pump_ep_desc, ota_cluster), TAG, "add ota cluster failed");

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

static esp_err_t zb_register_load_endpoint(ezb_af_device_desc_t device_desc)
{
    // Create the Load endpoint configuration
    ezb_af_ep_config_t load_ep_config = {
        .ep_id              = ZB_LOAD_ENDPOINT_ID,
        .app_profile_id     = EZB_AF_HA_PROFILE_ID,
    };

    // Create the Load endpoint
    ezb_af_ep_desc_t load_ep_desc = ezb_af_create_endpoint_desc(&load_ep_config);

    // Create load on/off cluster configuration
    static ezb_zcl_on_off_cluster_config_t load_onoff_cfg = {
        .on_off = false,
    };
    ezb_zcl_cluster_desc_t on_off_cluster = ezb_zcl_on_off_create_cluster_desc(&load_onoff_cfg, EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(load_ep_desc, on_off_cluster), TAG, "add load on/off cluster failed");

    // Create Electrical Measurement cluster
    ezb_zcl_cluster_desc_t em_cluster = ezb_zcl_electrical_measurement_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);

    static uint16_t default_u16 = 0;
    static uint32_t default_type = EZB_ZCL_ELECTRICAL_MEASUREMENT_MEASUREMENT_TYPE_DC_MEASUREMENT;
    static uint16_t mult_1 = 1;
    static uint16_t div_100 = 100;

    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_MEASUREMENT_TYPE_ID, &default_type);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_CURRENT_ID, &default_u16);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_AC_CURRENT_MULTIPLIER_ID, &mult_1);
    ezb_zcl_electrical_measurement_cluster_desc_add_attr(em_cluster, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_AC_CURRENT_DIVISOR_ID, &div_100);

    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(load_ep_desc, em_cluster), TAG, "add load electrical measurement cluster failed");

    ESP_RETURN_ON_ERROR(ezb_af_device_add_endpoint_desc(device_desc, load_ep_desc), TAG, "add load endpoint failed");
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

    // Create the Load endpoint
    zb_register_load_endpoint(device_desc);

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
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_steering_timer));

    esp_timer_create_args_t pairing_timer_args = {
        .callback = pairing_window_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "pairing_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&pairing_timer_args, &s_pairing_window_timer));

    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(ZB_PRIMARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(ZB_SECONDARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(zb_app_signal_handler));

    ESP_ERROR_CHECK(zb_register_endpoints());
    ezb_zcl_raw_command_handler_register(zb_raw_frame_handler);
    ezb_zcl_ota_upgrade_cluster_client_init(ZB_PUMP_1_ENDPOINT_ID);
    ezb_zcl_ota_upgrade_set_hw_version(ZB_PUMP_1_ENDPOINT_ID, ZB_MODEL_HW_VERSION);
    ezb_zcl_ota_upgrade_set_download_block_size(ZB_PUMP_1_ENDPOINT_ID, 50);

#ifdef OTA_FILE_VERSION
    ESP_LOGI(TAG, "OTA Current File Version before start: 0x%08lx", (unsigned long)g_ota_file_version);
#endif
    ezb_af_node_desc_set_manuf_code(0x1001);

    ESP_ERROR_CHECK(esp_zigbee_start(false));

#ifdef OTA_FILE_VERSION
    ESP_LOGI(TAG, "OTA Current File Version before start: 0x%08lx", (unsigned long)g_ota_file_version);
    // esp_zigbee_start() copies attribute values into its own internal memory pool.
    // Direct writes to our global pointer have no effect after this point.
    // We must use the official API to update the stack's internal copy.
    
    // The SDK's OTA init may have overwritten g_ota_file_version with 0xFFFFFFFF if flashed via USB.
    // Re-initialize it here before updating the stack's internal copy!
    g_ota_file_version = OTA_FILE_VERSION;
    g_ota_downloaded_file_version = OTA_FILE_VERSION;

    ezb_zcl_status_t st1 = ezb_zcl_set_attr_value(
        ZB_PUMP_1_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_OTA_UPGRADE, EZB_ZCL_CLUSTER_CLIENT,
        EZB_ZCL_ATTR_OTA_UPGRADE_CURRENT_FILE_VERSION_ID, EZB_ZCL_STD_MANUF_CODE,
        &g_ota_file_version, false);
    
    ezb_zcl_status_t st2 = ezb_zcl_set_attr_value(
        ZB_PUMP_1_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_OTA_UPGRADE, EZB_ZCL_CLUSTER_CLIENT,
        EZB_ZCL_ATTR_OTA_UPGRADE_DOWNLOADED_FILE_VERSION_ID, EZB_ZCL_STD_MANUF_CODE,
        &g_ota_downloaded_file_version, false);

    ESP_LOGI(TAG, "OTA File Version set via API after start: 0x%08lx (st1=0x%02x, st2=0x%02x)", (unsigned long)g_ota_file_version, st1, st2);
#endif

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

bool zigbee_is_ota_in_progress(void)
{
    return s_ota_in_progress;
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

void zigbee_factory_reset(void)
{
    ESP_LOGI(TAG, "Factory resetting Zigbee stack...");
    esp_zigbee_factory_reset();
}
