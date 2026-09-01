---
name: zigbee-cluster-mgmt
description: Procedure for creating, modifying, or reporting Zigbee 3.0 clusters, attributes, and endpoints in firmware and synchronizing Zigbee2MQTT external converters.
---

# Zigbee Cluster & Endpoint Management Guide

This skill provides step-by-step instructions for adding or updating Zigbee 3.0 endpoints and attributes in the Greenhouse Controller, and keeping Zigbee2MQTT in sync.

---

## 1. Modifying Firmware Clusters & Endpoints

All Zigbee endpoint definitions and stack callbacks reside in `src/main/zigbee_controller.c` and `src/main/zigbee_controller.h`.

### Step A: Declare or Modify Cluster in Endpoint List
1. Locate the endpoint initialization function in `zigbee_controller.c` (e.g. `zb_create_custom_endpoint(...)`).
2. Add or configure the target standard cluster (e.g. `ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT`, `ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT`).
3. Attach required attribute IDs (e.g., RMS Voltage `0x0505`, RMS Current `0x0508`, Active Power `0x050B`).

### Step B: Report Attribute Updates
When telemetry is updated (e.g. from the VE.Direct or SHT30 sensor tasks):
```c
esp_zb_zcl_attr_update_value(
    endpoint_id,
    cluster_id,
    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
    attribute_id,
    &attribute_value
);
```

### Step C: Handle Incoming Commands
For controllable clusters (e.g., `ESP_ZB_ZCL_CLUSTER_ID_ON_OFF` for Pump 1 or Load Switch):
1. Register the action callback in `esp_zb_custom_action_handler`.
2. Update local hardware state via `gpio_set_level()` and notify other tasks via FreeRTOS queue.
3. Report the updated attribute state back to the coordinator.

---

## 2. Synchronizing Zigbee2MQTT Converter

Whenever endpoints, clusters, or attribute mappings change in firmware, update `Zigbee2Mqtt/external_converters/greenhouse_controller.js`:

1. **`configure` Section**:
   - Bind the cluster to `coordinatorEndpoint`.
   - Call `configureReporting` with appropriate `minimumReportInterval`, `maximumReportInterval`, and `reportableChange`.

2. **`fromZigbee` Section**:
   - Add/update parsing logic to decode incoming attribute reports and return JSON telemetry keys.

3. **`toZigbee` Section**:
   - If introducing controllable switches or setpoints, add command converters to encode outgoing Zigbee frames.

4. **`exposes` Section**:
   - Expose the telemetry as numerical sensors, binary switches, or diagnostic entities in Home Assistant / Zigbee2MQTT.

---

## 3. Verification Checklist

- [ ] Devcontainer build succeeds: `devcontainer exec --workspace-folder . idf.py -C src build`.
- [ ] Endpoints in `src/main/zigbee_controller.h` match endpoints in `greenhouse_controller.js` (1: Pump/Battery, 2: Climate, 3: Solar, 4: Load).
- [ ] Attribute reporting changes and data types match exactly.
- [ ] [README.md](file:///README.md) table of Zigbee endpoints and clusters is updated.
