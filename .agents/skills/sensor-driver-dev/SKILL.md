---
name: sensor-driver-dev
description: Guidelines and patterns for implementing and debugging peripheral sensor and actuator drivers (I2C, UART, GPIO) in FreeRTOS tasks.
---

# Sensor and Peripheral Driver Development Guide

This skill details patterns for writing reliable FreeRTOS-based drivers for hardware peripherals (SHT30 I2C sensor, Victron VE.Direct UART parser, GPIO switches/LEDs).

---

## 1. I2C Sensor Driver Pattern (SHT30)

- **Source Files**: `src/main/sht30_sensor.c`, `src/main/sht30_sensor.h`.
- **Bus Initialization**: Initialize the I2C master bus once during startup using `i2c_new_master_bus()` or standard ESP-IDF driver.
- **Polling Loop**:
  1. Send measurement command with clock stretching or single-shot command.
  2. Non-blocking wait (`vTaskDelay(pdMS_TO_TICKS(50))`).
  3. Read raw 6-byte response (Temperature MSB/LSB + CRC, Humidity MSB/LSB + CRC).
  4. Verify CRC8 checksum on each 2-byte word before accepting data.
  5. Convert raw ticks to engineering units:
     $$\text{Temp} (^\circ\text{C}) = -45 + 175 \cdot \frac{S_T}{2^{16} - 1}$$
     $$\text{RH} (\%) = 100 \cdot \frac{S_{RH}}{2^{16} - 1}$$
  6. Dispatch values to the central telemetry queue.

---

## 2. UART Protocol Parser Pattern (Victron VE.Direct)

- **Source Files**: `src/main/ve_direct.c`, `src/main/ve_direct.h`.
- **Baud Rate**: 19200 baud, 8N1.
- **Parsing Strategy**:
  1. Stream ASCII records delimited by CRLF (`\r\n`).
  2. Parse key-value pairs (e.g. `V` for battery mV, `VPV` for panel mV, `PPV` for panel power W, `IL` for load current mA).
  3. Validate checksum field (the sum of all bytes in the frame modulo 256 must equal 0).
  4. On checksum failure, log `ESP_LOGW` and discard the malformed frame; do not crash the task.
  5. Update shared power telemetry structure safely via mutex or queue.

---

## 3. GPIO & Actuator Control Pattern

- **Source Files**: `src/main/gpio_controller.c`, `src/main/led.c`.
- **Relay/Pump Protection**:
  - Implement minimum off-time guards to protect pumps against rapid oscillation.
  - Maintain active-high or active-low configuration consistently.
- **Status LED Indication**:
  - Provide clear blink patterns for:
    - Joining network / pairing
    - Normal operation
    - Error / sensor failure
