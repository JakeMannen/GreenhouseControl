# Component CMakeLists File
# Core logic remains here.


# Register all source files and dependencies for the main application component.
idf_component_register(
    SRCS "zigbee_controller.c"
    "sht30_sensor.c"
    "gpio_controller.c"
    "ve_direct.c"
    "led.c"
    "config_manager.c"
    "main.c"

    INCLUDE_DIRS "." # Any necessary includes from this local component
    PRIV_REQUIRES driver nvs_flash esp_timer esp_driver_i2c led_strip esp-zigbee-lib app_update esp_partition
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    APP_VERSION_STR="${APP_VERSION}"
    OTA_FILE_VERSION=${OTA_FILE_VERSION}
    BUILD_DATE_STR="${BUILD_DATE}"
)