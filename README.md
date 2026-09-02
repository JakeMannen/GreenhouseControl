# Zigbee Greenhouse Irrigation Controller

## About the Project
This is an embedded system project for a greenhouse irrigation controller based on the ESP-IDF framework. It utilizes Zigbee, I2C, UART, and GPIO interfaces to communicate with and control various devices and sensors, including:
- Water pumps (MOSFET control with safety auto-off timer)
- SHT30 Temperature and humidity sensor (I2C)
- Physical manual override button & pairing button
- Status RGB LED (WS2812)
- Victron Solar panel charge controllers (VE.Direct UART)

## Hardware & Pinout

### Target Hardware
- **Microcontroller**: ESP32-C6 (intended hardware target with native IEEE 802.15.4 Zigbee support)

### Default GPIO Pin Mapping
Pin assignments can be customized via `idf.py menuconfig` under *Greenhouse Controller Configuration -> Hardware Pins*.

| Function | Pin (ESP32-C6 Default) | Interface / Type | Description |
|---|---|---|---|
| **Pump Output** | GPIO 20 | Digital Output (Active High) | Controls the MOSFET driving the irrigation water pump |
| **Pump Manual Button** | GPIO 21 | Digital Input (Pull-up) | Pushbutton to manually toggle the pump on/off |
| **Pairing / Reset Button** | GPIO 9 | Digital Input (Pull-up / BOOT) | Pushbutton to trigger Zigbee pairing / factory reset |
| **SHT30 SDA** | GPIO 6 | I2C SDA | Data line for SHT30 Temperature & Humidity sensor |
| **SHT30 SCL** | GPIO 7 | I2C SCL | Clock line for SHT30 Temperature & Humidity sensor |
| **VE.Direct RX** | GPIO 17 | UART RX (19200 baud) | Receives VE.Direct text protocol from Victron MPPT controller |
| **Status RGB LED** | GPIO 8 | WS2812 (RMT) | Built-in RGB addressable LED for system status |

---

## Device Operation & Features

### Physical Buttons & External Switch Modes
- **Pump Manual Switch (GPIO 21)**: Configurable external switch mode via Zigbee (`genOnOffSwitchCfg`):
  - **PRESS mode (Default)**: Momentary button press toggles the pump ON (runs for the configurable runtime / safety timeout, default 10 minutes) or turns it OFF.
  - **HOLD mode**: Holding the switch closed keeps the pump ON; releasing the switch immediately stops the pump.
- **Pairing & Factory Reset (GPIO 9 / BOOT)**: Press **3 times within a 2-second window** to reset Zigbee network credentials and enter Zigbee pairing mode (searches for network for 3 minutes).

### Pump Safety Auto-Off Timer & Configurable Runtime
To prevent accidental water overflows or running the pump dry, the controller includes a hardware safety timer (default: **10 minutes / 600 seconds**). This runtime is fully configurable via Zigbee (`genOnOff` attribute `OnTime` `0x4001`) and persisted in NVS. If the pump is turned on (either via Zigbee or manual button), it will automatically turn off once this duration expires.

### Status LED Indications (WS2812)
The onboard RGB LED provides visual feedback on device connectivity:
- **Blinking Blue** (500 ms interval): Searching for Zigbee network (Pairing Mode / Steering).
- **Fast Blinking Green** (4 pulses): Successfully joined a new Zigbee network.
- **Single Slow Green Pulse** (2 seconds): Rejoined an existing Zigbee network upon boot.
- **Fast Blinking Red** (4 pulses): Network join or pairing failed / timeout.
- **Solid Yellow**: System warning state.

---

## Dependencies & Environment
- **ESP-IDF**: Version >= v6.1
- **esp_zigbee_lib**: Version >= v2.0.1
- **Dev Containers Environment**: The build environment is fully containerized.

### Recommended Setup
It is recommended to develop using **VS Code** with the **Dev Containers**, **Remote Development**, and **ESP-IDF** extensions.

To enable serial flashing inside the dev container on Windows/Linux, refer to the [Espressif Dev Containers Guide](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/additionalfeatures/docker-container.html).

---

## How to Build, Flash & Monitor

All `idf.py` commands targeting the `src` directory can be executed via the dev container.

If the dev container is not currently running, start it from the project root:
```bash
devcontainer up --workspace-folder .
```

### Build
To compile the project:
```bash
devcontainer exec --workspace-folder . idf.py -C src build
```

### Flash
To flash the compiled firmware to the ESP32-C6:
```bash
devcontainer exec --workspace-folder . idf.py -C src flash
```
*(Note: You can specify the serial port if needed, e.g., `devcontainer exec --workspace-folder . idf.py -C src -p COM3 flash`)*

### Monitor
To view serial monitor logs:
```bash
devcontainer exec --workspace-folder . idf.py -C src monitor
```

### Configuration Menu
To configure pinouts, reporting intervals, and Zigbee settings:
```bash
devcontainer exec --workspace-folder . idf.py -C src menuconfig
```

---

## Zigbee Endpoints & Clusters

The controller exposes four endpoints under the Zigbee Home Automation (HA) profile (`0x0104`):

### Endpoint 1: Pump & Power Configuration
* **Endpoint ID**: `1`
* **Device ID**: `0x0303`

| Cluster ID | Cluster Name | Attributes | Description |
|---|---|---|---|
| `0x0000` | Basic | `0x0000` (ZCL Version)<br>`0x0001` (App Version)<br>`0x0003` (HW Version)<br>`0x0004` (Manufacturer Name)<br>`0x0005` (Model Identifier)<br>`0x0006` (Date Code)<br>`0x0007` (Power Source)<br>`0x4000` (SW Build ID) | Device metadata, versioning, date code, and power source (`Battery`) |
| `0x0006` | On/Off | `0x0000` (OnOff)<br>`0x4001` (OnTime) | Water pump 1 output control (ON / OFF) and configurable auto-off button runtime in 0.1s units (default: 6000 = 600s / 10 min), persisted in NVS |
| `0x0007` | On/Off Switch Configuration | `0x0000` (SwitchType)<br>`0x0010` (SwitchActions) | External manual switch mode configuration (`PRESS` / Toggle vs `HOLD` / Momentary), persisted in NVS |
| `0x0001` | Power Configuration | `0x0020` (BatteryVoltage)<br>`0x0021` (BatteryPercentageRemaining) | Battery voltage (in 100mV units) and state-of-charge percentage (calculated using LiFePO4 discharge curve) |
| `0x0019` | OTA Upgrade (Client) | `0x0002` (Current File Version)<br>`0x0004` (Downloaded File Version) | Over-The-Air firmware updates |

### Endpoint 2: Climate & Environment
* **Endpoint ID**: `2`

| Cluster ID | Cluster Name | Attributes | Description |
|---|---|---|---|
| `0x0402` | Temperature Measurement | `0x0000` (MeasuredValue)<br>`0x0001` (MinMeasuredValue)<br>`0x0002` (MaxMeasuredValue) | SHT30 Ambient Temperature in 0.01 °C resolution (-40.00 °C to 125.00 °C). Supports configurable ZCL reporting in Zigbee2MQTT (default: min 10s, max 3600s, delta 0.50 °C). |
| `0x0405` | Relative Humidity Measurement | `0x0000` (MeasuredValue)<br>`0x0001` (MinMeasuredValue)<br>`0x0002` (MaxMeasuredValue) | SHT30 Ambient Relative Humidity in 0.01 % resolution (0.00 % to 100.00 %). Supports configurable ZCL reporting in Zigbee2MQTT (default: min 10s, max 3600s, delta 1.00 %). |

### Endpoint 3: Solar Panel Monitoring
* **Endpoint ID**: `3`

| Cluster ID | Cluster Name | Attributes | Description |
|---|---|---|---|
| `0x0B04` | Electrical Measurement | `0x0000` (MeasurementType = DC)<br>`0x0505` (RMSVoltage)<br>`0x0508` (RMSCurrent)<br>`0x050B` (ActivePower)<br>`0x0600`/`0x0601` (Voltage Multiplier / Divisor)<br>`0x0602`/`0x0603` (Current Multiplier / Divisor)<br>`0x0604`/`0x0605` (Power Multiplier / Divisor) | Victron VE.Direct solar panel telemetry (Panel Voltage in 0.01V, Charge Current in 0.01A, Solar Power in 0.1W) |

### Endpoint 4: Load Output & Current Monitoring
* **Endpoint ID**: `4`

| Cluster ID | Cluster Name | Attributes | Description |
|---|---|---|---|
| `0x0006` | On/Off | `0x0000` (OnOff) | Victron VE.Direct Load Output state (`LOAD` - ON / OFF) |
| `0x0B04` | Electrical Measurement | `0x0000` (MeasurementType = DC)<br>`0x0508` (RMSCurrent)<br>`0x0602`/`0x0603` (Current Multiplier / Divisor) | Victron VE.Direct Load Current (`IL` in 0.01A) |

---

## Over-The-Air (OTA) Updates

This device supports Zigbee OTA updates to flash new firmware wirelessly without needing physical serial access. 

The GitHub Actions CI/CD pipeline automatically compiles `.ota` firmware files and attaches them to GitHub Releases. Because this is a custom local device, OTA updates are uploaded directly through Zigbee2MQTT.

**Key Technical Details:**
* **Versioning Scheme**: The 32-bit OTA file version follows the standard Silicon Labs / Zigbee 4-byte layout: `[Major | Build | Minor | Patch]`. The Zigbee Basic Cluster `application_version` (uint8) is derived from the Major byte (clamped to minimum 1 per ZCL spec). Local builds default to `Firmware-ID: 0.0.1` and `OTA_FILE_VERSION: 0x00000001`. Automated CI release builds use semantic versioning (e.g., `v1.2.3` → `0x01000203`).
* **Block Size**: 50 bytes per chunk (fits in a single unfragmented 802.15.4 frame).
* **Partition Pre-Erase**: Target flash partition is pre-erased upfront upon transfer start to eliminate flash erase stalls during chunk reception.
* **Reporting Suppression**: Sensor attribute reporting is automatically paused during OTA downloads to prioritize radio bandwidth.

**How to perform an OTA update:**
1. Navigate to the [Releases page](../../releases) on this GitHub repository.
2. Download the `.ota` file (e.g., `greenhouse_controller-v1.2.3.ota`) from the latest release.
3. Open your **Zigbee2MQTT** web frontend.
4. Go to **OTA** in the top navigation bar.
5. Under the **Local OTA file** section, upload the downloaded `.ota` file.
6. Trigger the OTA update for your device.

*(Note: To downgrade or force-install an older firmware build, trigger the update via the Zigbee2MQTT MQTT downgrade endpoint `zigbee2mqtt/bridge/request/device/ota_update/update/downgrade` with payload `{"id": "<device_id>"}`).*

---

## Home Assistant & Zigbee2MQTT External Converter

Because this is a customized DIY device, an external converter is provided to integrate all features with Zigbee2MQTT and Home Assistant.

### Installation
1. Copy the provided `Zigbee2Mqtt/external_converters/greenhouse_controller.js` file to your Zigbee2MQTT configuration folder (under `external_converters/`).
2. Copy `Zigbee2Mqtt/device_icons/greenhouse_controller_icon.png` to your Zigbee2MQTT `device_icons/` folder.
3. Add the external converter to your Zigbee2MQTT `configuration.yaml`:
   ```yaml
   external_converters:
     - greenhouse_controller.js
   ```
4. Restart Zigbee2MQTT.
5. When the controller joins the Zigbee network, configure the device icon in the Zigbee2MQTT frontend at **<device_name> -> Settings (specific) -> Icon** (`device_icons/greenhouse_controller_icon.png`).

---

## Development Workflow & GitFlow

The project strictly follows a simplified **GitFlow** branching model:

```
feature/* ──(PR)──> dev ──(Release PR)──> main
```

* **`main`**: Production & release branch. Receives merges only from `dev` via Pull Requests. Every merge to `main` triggers automated semantic tagging, GitHub Releases, and Zigbee OTA binary generation.
* **`dev`**: Default integration branch. All feature branches branch off from `dev` and are merged back into `dev` via Pull Requests. Merges to `dev` generate pre-release OTA builds.
* **`feat/*`** / **`feature/*`**: Short-lived branches used for developing features and fixes.

### CI/CD Automation & Quality Gates
* **PR Branch Flow Validation** (`.github/workflows/branch-check.yml`): Enforces that feature PRs target `dev`, and PRs to `main` originate exclusively from `dev`.
* **PR Semantic Title Linter** (`.github/workflows/pr-linter.yml`): Validates Conventional Commit PR title formatting (e.g., `feat: ...`, `fix: ...`, `docs: ...`).
* **Build & Release Pipeline** (`.github/workflows/build-and-release.yml`): Firmware builds and releases occur exclusively on merge to `dev` (pre-release OTA) and `main` (production release OTA). No builds run on feature branches or unmerged PRs.