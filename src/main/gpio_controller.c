#include "gpio_controller.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

static const char *TAG = "GPIO_DRV";

static void (*s_pump_state_changed_cb)(bool is_on) = NULL;
static void (*s_factory_reset_cb)(void) = NULL;
static TimerHandle_t s_safety_timer = NULL;
static QueueHandle_t s_gpio_evt_queue = NULL;
static bool s_pump_state = false;
static gh_switch_mode_t s_switch_mode = GH_SWITCH_MODE_PRESS;
static uint32_t s_pump_runtime_sec = CONFIG_GH_PUMP_SAFETY_TIMEOUT_MIN * 60;
static int64_t s_last_pump_btn_press_time = 0;
static int64_t s_last_pairing_btn_press_time = 0;
static int64_t s_first_pairing_btn_press_time = 0;
static uint8_t s_pairing_btn_press_count = 0;

static void safety_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGW(TAG, "Pump safety timeout reached! Automatically shutting off pump.");
    gpio_set_pump_state(false);
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(s_gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_button_task(void* arg) {
    uint32_t io_num;
    while (1) {
        if (xQueueReceive(s_gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int64_t now = esp_timer_get_time(); // Time in microseconds
            
            if (io_num == CONFIG_GH_BUTTON_GPIO_PIN) {
                int level = gpio_get_level(CONFIG_GH_BUTTON_GPIO_PIN);
                if (now - s_last_pump_btn_press_time > CONFIG_GH_BUTTON_DEBOUNCE_US) {
                    s_last_pump_btn_press_time = now;
                    if (s_switch_mode == GH_SWITCH_MODE_HOLD) {
                        if (level == 0) {
                            ESP_LOGI(TAG, "Pump switch pressed (held) in HOLD mode! Turning pump ON.");
                            gpio_set_pump_state(true);
                        } else {
                            ESP_LOGI(TAG, "Pump switch released in HOLD mode! Turning pump OFF.");
                            gpio_set_pump_state(false);
                        }
                    } else { // GH_SWITCH_MODE_PRESS
                        if (level == 0) {
                            ESP_LOGI(TAG, "Pump manual button pressed in PRESS mode! Toggling pump state.");
                            gpio_set_pump_state(!s_pump_state);
                        }
                    }
                }
            } else if (io_num == CONFIG_GH_PAIRING_BUTTON_GPIO_PIN) {
                // Debounce time from Kconfig
                if (now - s_last_pairing_btn_press_time > CONFIG_GH_BUTTON_DEBOUNCE_US) {
                    s_last_pairing_btn_press_time = now;
                    
                    if (now - s_first_pairing_btn_press_time > 2000000) { // 2 seconds window
                        s_pairing_btn_press_count = 1;
                        s_first_pairing_btn_press_time = now;
                    } else {
                        s_pairing_btn_press_count++;
                    }

                    if (s_pairing_btn_press_count >= 3) {
                        ESP_LOGI(TAG, "Pairing button pressed 3 times! Triggering factory reset.");
                        if (s_factory_reset_cb) {
                            s_factory_reset_cb();
                        }
                        s_pairing_btn_press_count = 0;
                    } else {
                        ESP_LOGI(TAG, "Pairing button pressed %d times.", s_pairing_btn_press_count);
                    }
                }
            }
        }
    }
}

esp_err_t gpio_drivers_init(void (*pump_state_changed_cb)(bool is_on), void (*factory_reset_cb)(void)) {
    s_pump_state_changed_cb = pump_state_changed_cb;
    s_factory_reset_cb = factory_reset_cb;
    s_switch_mode = config_manager_get()->switch_mode;

    // Configure pump pin (Output, pull-down enabled, active high)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_GH_PUMP_GPIO_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_GH_PUMP_GPIO_PIN, 0); // Start with pump off
    s_pump_state = false;

    // Configure pump manual button pin (Input, pull-up, interrupt on both edges to detect press and release)
    io_conf.pin_bit_mask = (1ULL << CONFIG_GH_BUTTON_GPIO_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    gpio_config(&io_conf);

    // Configure pairing button pin (Input, pull-up, interrupt on falling edge)
    io_conf.pin_bit_mask = (1ULL << CONFIG_GH_PAIRING_BUTTON_GPIO_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&io_conf);

    // Create event queue for GPIO ISR
    s_gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (s_gpio_evt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create GPIO event queue");
        return ESP_ERR_NO_MEM;
    }

    // Start GPIO button task
    xTaskCreate(gpio_button_task, "gpio_button_task", 3072, NULL, 10, NULL);

    // Install GPIO ISR service
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        return err;
    }

    // Add ISR handler for pump manual button GPIO pin
    err = gpio_isr_handler_add(CONFIG_GH_BUTTON_GPIO_PIN, gpio_isr_handler, (void*)CONFIG_GH_BUTTON_GPIO_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add pump button ISR handler: %s", esp_err_to_name(err));
        return err;
    }

    // Add ISR handler for pairing button GPIO pin
    err = gpio_isr_handler_add(CONFIG_GH_PAIRING_BUTTON_GPIO_PIN, gpio_isr_handler, (void*)CONFIG_GH_PAIRING_BUTTON_GPIO_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add pairing button ISR handler: %s", esp_err_to_name(err));
        return err;
    }

    // Create FreeRTOS timer for pump safety timeout
    s_pump_runtime_sec = (config_manager_get()->pump_runtime_sec > 0) ? 
                         config_manager_get()->pump_runtime_sec : 
                         (CONFIG_GH_PUMP_SAFETY_TIMEOUT_MIN * 60);
    s_safety_timer = xTimerCreate("pump_safety_timer",
                                  pdMS_TO_TICKS(s_pump_runtime_sec * 1000),
                                  pdFALSE, // One-shot
                                  (void*)0,
                                  safety_timer_callback);
    if (s_safety_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create pump safety timer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GPIO driver initialized successfully (switch mode: %s, runtime: %lu s)",
             s_switch_mode == GH_SWITCH_MODE_HOLD ? "HOLD" : "PRESS", (unsigned long)s_pump_runtime_sec);
    return ESP_OK;
}

void gpio_set_pump_state(bool is_on) {
    if (s_pump_state == is_on) {
        return; // State already matches
    }

    s_pump_state = is_on;
    gpio_set_level(CONFIG_GH_PUMP_GPIO_PIN, is_on ? 1 : 0);
    ESP_LOGI(TAG, "Pump hardware set to %s", is_on ? "ON (GPIO high)" : "OFF (GPIO low)");

    if (is_on) {
        if (s_safety_timer != NULL) {
            xTimerStart(s_safety_timer, 0);
            ESP_LOGI(TAG, "Pump safety timer started for %lu seconds", (unsigned long)s_pump_runtime_sec);
        }
    } else {
        if (s_safety_timer != NULL) {
            xTimerStop(s_safety_timer, 0);
            ESP_LOGI(TAG, "Pump safety timer stopped");
        }
    }

    // Notify listeners of state change
    if (s_pump_state_changed_cb != NULL) {
        s_pump_state_changed_cb(is_on);
    }
}

bool gpio_get_pump_state(void) {
    return s_pump_state;
}

void gpio_set_switch_mode(gh_switch_mode_t mode) {
    s_switch_mode = mode;
    ESP_LOGI(TAG, "External switch mode set to: %s", mode == GH_SWITCH_MODE_HOLD ? "HOLD" : "PRESS");
    if (mode == GH_SWITCH_MODE_HOLD) {
        int level = gpio_get_level(CONFIG_GH_BUTTON_GPIO_PIN);
        // If switch is not held (level == 1) but pump is on, turn it off
        if (level == 1 && s_pump_state) {
            gpio_set_pump_state(false);
        }
    }
}

gh_switch_mode_t gpio_get_switch_mode(void) {
    return s_switch_mode;
}

void gpio_set_pump_runtime(uint32_t runtime_sec) {
    if (runtime_sec == 0) {
        runtime_sec = 1;
    }
    s_pump_runtime_sec = runtime_sec;
    ESP_LOGI(TAG, "Pump runtime configured to %lu seconds", (unsigned long)runtime_sec);
    if (s_safety_timer != NULL) {
        xTimerChangePeriod(s_safety_timer, pdMS_TO_TICKS(runtime_sec * 1000), 0);
        if (!s_pump_state) {
            xTimerStop(s_safety_timer, 0);
        }
    }
}

uint32_t gpio_get_pump_runtime(void) {
    return s_pump_runtime_sec;
}
