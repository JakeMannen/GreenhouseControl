
# Project main features

This is a ESP-IDF embedded system project for a greenhouse irrigation controller. Main features and technologies are Zigbee, I2C, UART and GPIO that communicates with devices & sensors such as water pumps, temperature/humidity, On/Off switches and Solar panel controller.

## Restrictions

NEVER install anything on the host machine like python, esp-idf or other tools

## Device

The ESP32C6 is the intended hardware for this controller software

## Frameworks to use

- ESP-IDF >= v6.1
- esp_zigbee_lib v2.0.1

## Executing commands

When esp-idf commands like "idf.py build" or "idf.py menuconfig" needs to be used, it must be executed in the /src/.devcontainer using "devcontainer exec <COMMAND>". If it is not running, start it by executing "devcontainer up --workspace-folder ." in the project root folder.

Examples:
- devcontainer exec idf.py build
- devcontainer exec idf.py flash

## Resources

ESP-IDF source code: https://github.com/espressif/esp-idf
esp_zigbee_lib source code: https://github.com/espressif/esp-zigbee-sdk/tree/main/components/esp-zigbee-lib
esp_zigbee_lib migration from v1.X to v2.X: https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/migration-guide/v2.x/index.html