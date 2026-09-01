# Zigbee Protocol & Cluster Standards

## 1. Zigbee 3.0 Cluster Architecture
- Use `esp_zigbee_lib` v2.x APIs for cluster creation, attribute registration, and endpoint initialization.
- **Endpoint Structure**:
  - **Endpoint 1 (Pump / Main Control & Power)**:
    - `genBasic` (Basic information, Model ID: `GreenHouse Controller`, Vendor: `Espressif`, Power Source)
    - `genOnOff` (Water Pump 1 output control)
    - `genPowerCfg` (Battery Voltage, Battery Percentage Remaining)
    - `genOta` (OTA Upgrade Client cluster)
  - **Endpoint 2 (Climate)**:
    - `msTemperatureMeasurement` (Measured Value: $0.01^\circ\text{C}$ resolution, `int16_t`)
    - `msRelativeHumidity` (Measured Value: $0.01\%$ resolution, `uint16_t`)
  - **Endpoint 3 (Solar Inverter / Charger)**:
    - `haElectricalMeasurement` (RMS Voltage, RMS Current, Active Power)
  - **Endpoint 4 (Load Output)**:
    - `genOnOff` (Load switch state)
    - `haElectricalMeasurement` (Load RMS Current)

## 2. Attribute Reporting
- Set standard minimum/maximum reporting intervals and reportable change thresholds for analog measurements:
  - Temperature: min `10s`, max `3600s`, reportable change `50` ($0.5^\circ\text{C}$).
  - Humidity: min `10s`, max `3600s`, reportable change `100` ($1.0\%$).
  - Battery: min `10s`, max `3600s`, reportable change `1` (0.1V / 0.5%).
  - Solar / Electrical: min `10s`, max `3600s`, reportable change `10` ($0.1\text{V}$, $0.1\text{A}$, $1.0\text{W}$).

## 3. Coordinator Binding & Zigbee2MQTT Synchronization
- Whenever a cluster or endpoint is added or modified in `src/main/zigbee_controller.c`:
  - The corresponding binding and configure logic must be updated in `Zigbee2Mqtt/external_converters/greenhouse_controller.js`.
  - `fromZigbee`, `toZigbee`, and `exposes` sections in `greenhouse_controller.js` must be kept in exact parity with firmware telemetry and commands.
