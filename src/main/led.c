#include "led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led_strip.h"

static const char *TAG = "LED";

static led_strip_handle_t s_led_strip;
static TaskHandle_t s_blink_task_handle = NULL;
static uint8_t            s_red = 255, s_green = 255, s_blue = 255, s_level = 255;
static uint8_t             s_blink_brightness = CONFIG_LED_BLINK_BRIGHTNESS;

static void light_driver_blink_loop_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Blink task started with params: R=%d, G=%d, B=%d, delay=%d ms, num_blinks=%d",
             ((light_driver_blink_params_t *)pvParameters)->red,
             ((light_driver_blink_params_t *)pvParameters)->green,
             ((light_driver_blink_params_t *)pvParameters)->blue,
             ((light_driver_blink_params_t *)pvParameters)->delay_ms,
             ((light_driver_blink_params_t *)pvParameters)->num_blinks);

    light_driver_blink_params_t *params = (light_driver_blink_params_t *)pvParameters;

    if(params->num_blinks > 0) {
        for (uint8_t i = 0; i < params->num_blinks; ++i) {
            light_driver_set_color_RGB(params->red, params->green, params->blue);
            vTaskDelay(pdMS_TO_TICKS(params->delay_ms));

            light_driver_set_power(false);
            vTaskDelay(pdMS_TO_TICKS(params->delay_ms));
        }
        light_driver_set_power(false);
    }
    else {
        
        while (1) {
            light_driver_set_color_RGB(params->red, params->green, params->blue);
            vTaskDelay(pdMS_TO_TICKS(params->delay_ms));

            light_driver_set_power(false);
            vTaskDelay(pdMS_TO_TICKS(params->delay_ms));
        }
    }

    s_blink_task_handle = NULL;
    vTaskDelete(NULL);
}

void light_driver_stop_blink()
{
    if (s_blink_task_handle != NULL) {

        if (xTaskGetCurrentTaskHandle() == s_blink_task_handle) {
            return; 
        }

        vTaskDelete(s_blink_task_handle);
        s_blink_task_handle = NULL;
        
        light_driver_set_power(false); 
    }
}

void light_driver_start_blink(uint8_t red, uint8_t green, uint8_t blue, uint32_t delay_ms, uint8_t num_blinks)
{
    light_driver_stop_blink();

    static light_driver_blink_params_t blink_params;

    blink_params.red = red;
    blink_params.green = green;
    blink_params.blue = blue;
    blink_params.delay_ms = delay_ms;
    blink_params.num_blinks = num_blinks;

    xTaskCreate(light_driver_blink_loop_task, "light_driver_blink_task", 3072, &blink_params, 10, &s_blink_task_handle);
}

void light_driver_set_color_xy(uint16_t color_current_x, uint16_t color_current_y)
{
    light_driver_stop_blink();

    float red_f = 0, green_f = 0, blue_f = 0, color_x, color_y;
    color_x = (float)color_current_x / 65535;
    color_y = (float)color_current_y / 65535;
    /* assume color_Y is full light level value 1  (0-1.0) */
    float color_X = color_x / color_y;
    float color_Z = (1 - color_x - color_y) / color_y;
    /* change from xy to linear RGB NOT sRGB */
    XYZ_to_RGB(color_X, 1, color_Z, red_f, green_f, blue_f);
    float ratio = (float)s_level / 255;
    s_red       = (uint8_t)(red_f * (float)255);
    s_green     = (uint8_t)(green_f * (float)255);
    s_blue      = (uint8_t)(blue_f * (float)255);
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, s_red * ratio, s_green * ratio, s_blue * ratio));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}

void light_driver_set_color_hue_sat(uint8_t hue, uint8_t sat)
{
    light_driver_stop_blink();

    float red_f, green_f, blue_f;
    HSV_to_RGB(hue, sat, UINT8_MAX, red_f, green_f, blue_f);
    float ratio = (float)s_level / 255;
    s_red       = (uint8_t)red_f;
    s_green     = (uint8_t)green_f;
    s_blue      = (uint8_t)blue_f;
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, s_red * ratio, s_green * ratio, s_blue * ratio));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}

void light_driver_set_color_RGB(uint8_t red, uint8_t green, uint8_t blue)
{
    light_driver_stop_blink();

    float ratio = (float)s_level / 255;
    s_red       = red;
    s_green     = green;
    s_blue      = blue;
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, red * ratio, green * ratio, blue * ratio));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}

void light_driver_set_power(bool power)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, s_red * power, s_green * power, s_blue * power));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}

void light_driver_set_level(uint8_t level)
{
    s_level     = level;
    float ratio = (float)s_level / 255;
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, s_red * ratio, s_green * ratio, s_blue * ratio));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}

void light_driver_set_ok(uint32_t duration)
{
    light_driver_start_blink(0, s_blink_brightness, 0, 2000, 1);
}

void light_driver_set_success()
{
    light_driver_start_blink(0, s_blink_brightness, 0, 200, 4);
}

void light_driver_set_warning()
{
    light_driver_set_color_RGB(s_blink_brightness, s_blink_brightness, 0); // Yellow for warning
}

void light_driver_set_error()
{
    light_driver_start_blink(s_blink_brightness, 0, 0, 200, 4); // Red blinking for error
}

void light_driver_set_connecting()
{
    light_driver_start_blink(0, 0, s_blink_brightness, 500, 0);
}

void light_driver_init(bool power)
{
    led_strip_config_t led_strip_conf = {
        .max_leds       = CONFIG_EXAMPLE_STRIP_LED_NUMBER,
        .strip_gpio_num = CONFIG_EXAMPLE_STRIP_LED_GPIO,
        .led_model      = LED_MODEL_WS2812, // WS2812 RGB LED strip
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    led_strip_rmt_config_t rmt_conf = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_strip_conf, &rmt_conf, &s_led_strip));

    light_driver_set_power(power);
}
