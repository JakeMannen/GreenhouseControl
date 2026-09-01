#include "config_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "CONFIG_MANAGER";
static const char *NVS_NAMESPACE = "gh_config";

static app_config_t s_config;

static void set_default_config(app_config_t *cfg) {
    cfg->report_threshold_battery_mv = 100;
    cfg->report_threshold_panel_mv = 500;
    cfg->report_threshold_charge_current_ma = 100;
    cfg->report_threshold_panel_power_w = 2;
    cfg->report_threshold_load_current_ma = 50;

    cfg->report_interval_max_us = 300000000LL;
    cfg->report_interval_min_us = 10000000LL;

    cfg->batt_curve_100_mv = 13600;
    cfg->batt_curve_90_mv = 13300;
    cfg->batt_curve_70_mv = 13200;
    cfg->batt_curve_40_mv = 13100;
    cfg->batt_curve_20_mv = 12800;
    cfg->batt_curve_0_mv = 12000;

    cfg->switch_mode = GH_SWITCH_MODE_PRESS;
}

esp_err_t config_manager_init(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    size_t required_size = sizeof(app_config_t);
    err = nvs_get_blob(nvs_handle, "app_cfg", &s_config, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND || required_size != sizeof(app_config_t)) {
        ESP_LOGI(TAG, "Configuration not found or size changed, creating defaults...");
        set_default_config(&s_config);
        
        err = nvs_set_blob(nvs_handle, "app_cfg", &s_config, sizeof(app_config_t));
        if (err == ESP_OK) {
            nvs_commit(nvs_handle);
            ESP_LOGI(TAG, "Default configuration saved");
        } else {
            ESP_LOGE(TAG, "Failed to save default config: %s", esp_err_to_name(err));
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading config from NVS: %s", esp_err_to_name(err));
        set_default_config(&s_config);
    } else {
        ESP_LOGI(TAG, "Configuration loaded successfully from NVS");
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

const app_config_t* config_manager_get(void) {
    return &s_config;
}

esp_err_t config_manager_save(const app_config_t *new_config) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    s_config = *new_config;

    err = nvs_set_blob(nvs_handle, "app_cfg", &s_config, sizeof(app_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err;
}
