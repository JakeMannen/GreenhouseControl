# Zigbee Greenhouse Irrigation Controller

## About the Project
This is an embedded system project for a greenhouse irrigation controller based on the ESP-IDF framework. It utilizes Zigbee, I2C, UART, and GPIO interfaces to communicate with and control various devices and sensors, including:
- Water pumps (MOSFET)
- SHT30 Temperature and humidity sensor
- On/Off switch for pump
- Victron Solar panel controllers (VE.direct)

## Hardware Needed
- **Microcontroller**: ESP32-C6 (intended hardware target)

## Dependencies Needed
- **ESP-IDF**: Version >= v6.1
- **esp_zigbee_lib**: Version v2.0.1
- **Dev Containers Environment**: The build environment is containerized.

## Recommended Setup
It is recommended to use the project with **VS Code** and the extensions **Dev Containers, Remote Development & ESP-IDF**

To have the ESP board accessible inside the devcontainer, see: https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/additionalfeatures/docker-container.html

## How to Build, Flash & Monitor
All `idf.py` commands must be executed within the devcontainer located in `/src/.devcontainer`. 
If the devcontainer is not currently running, start it from the project root folder by executing:
```bash
devcontainer up --workspace-folder .
```

### Build
To compile the project, run:
```bash
devcontainer exec idf.py build
```

### Flash
To flash the compiled firmware to the ESP32-C6, run:
```bash
devcontainer exec idf.py flash
```
*(Note: You might need to specify the serial port, e.g., `devcontainer exec idf.py -p COM3 flash`)*

### Monitor
To monitor the serial output from the device, run:
```bash
devcontainer exec idf.py monitor
```
*(Note: You might need to specify the serial port, e.g., `devcontainer exec idf.py -p COM3 monitor`)*

### Configuration
If you need to change project configuration settings, run:
```bash
devcontainer exec idf.py menuconfig
```

## Zigbee Endpoints & Clusters

The controller exposes four endpoints under the Zigbee Home Automation (HA) profile (`0x0104`):

### Endpoint 1: Pump & System Control
* **Endpoint ID**: `1`
* **Device ID**: `0x0303`

| Cluster ID | Cluster Name | Attributes | Description |
|---|---|---|---|
| `0x0000` | Basic | `0x0000` (ZCL Version)<br>`0x0003` (HW Version)<br>`0x0004` (Manufacturer Name)<br>`0x0005` (Model Identifier)<br>`0x0007` (Power Source) | Device metadata and power source (`Battery`) |
| `0x0006` | On/Off | `0x0000` (OnOff) | Water pump 1 output control (ON / OFF) |
| `0x0001` | Power Configuration | `0x0020` (BatteryVoltage)<br>`0x0021` (BatteryPercentageRemaining) | Battery voltage (in 100mV units) and state of charge percentage |

### Endpoint 2: Climate & Environment
* **Endpoint ID**: `2`

| Cluster ID | Cluster Name | Attributes | Description |
|---|---|---|---|
| `0x0402` | Temperature Measurement | `0x0000` (MeasuredValue)<br>`0x0001` (MinMeasuredValue)<br>`0x0002` (MaxMeasuredValue) | SHT30 Ambient Temperature in 0.01 °C resolution (-40.00 °C to 125.00 °C) |
| `0x0405` | Relative Humidity Measurement | `0x0000` (MeasuredValue)<br>`0x0001` (MinMeasuredValue)<br>`0x0002` (MaxMeasuredValue) | SHT30 Ambient Relative Humidity in 0.01 % resolution (0.00 % to 100.00 %) |

### Endpoint 3: Solar Panel Monitoring
* **Endpoint ID**: `3`

| Cluster ID | Cluster Name | Attributes | Description |
|---|---|---|---|
| `0x0B04` | Electrical Measurement | `0x0000` (MeasurementType = DC)<br>`0x0505` (RMSVoltage)<br>`0x0508` (RMSCurrent)<br>`0x050B` (ActivePower)<br>`0x0600`/`0x0601` (Voltage Multiplier / Divisor)<br>`0x0602`/`0x0603` (Current Multiplier / Divisor)<br>`0x0604`/`0x0605` (Power Multiplier / Divisor) | Victron VE.Direct solar panel telemetry (Panel/Battery Voltage in V, Current in A, Power in W) |

### Endpoint 4: Load Output & Current Monitoring
* **Endpoint ID**: `4`

| Cluster ID | Cluster Name | Attributes | Description |
|---|---|---|---|
| `0x0006` | On/Off | `0x0000` (OnOff) | Victron VE.Direct Load Output state (`LOAD` - ON / OFF) |
| `0x0B04` | Electrical Measurement | `0x0000` (MeasurementType = DC)<br>`0x0508` (RMSCurrent)<br>`0x0602`/`0x0603` (Current Multiplier / Divisor) | Victron VE.Direct Load Current (`IL` in A) |

## Home Assistant Zigbee2Mqtt external converter
As this is a highly customized device it does not belong in the official Zigbee2Mqtt repository of supported devices.

If you want to have the device supported in your local installation of Home Assistant, you can add the included external converter folders, **external_converters/ & device_icons/** to the **zigbe2mqtt** folder (where its configuration.yaml is located).

When the device has joined the zigbee network, set the included icon at **<your_device_name>->Settings->icon** (device_icons/greenhouse_controller_image.png)