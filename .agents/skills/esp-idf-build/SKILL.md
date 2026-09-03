---
name: esp-idf-build
description: Build, clean, flash, and test the GreenhouseControl ESP-IDF project inside the devcontainer. Use when the user asks to compile, build, flash, or run firmware diagnostics.
---

# ESP-IDF Build & Devcontainer Guide

This skill documents how to build, test, and manage this ESP-IDF project inside the containerized environment.

## 1. Devcontainer Execution Workflow

All ESP-IDF builds and tools MUST run inside the project's devcontainer to avoid mutating the host system.

### A. Ensure Devcontainer is Running
Before running any `idf.py` commands, verify the container is up:
```powershell
devcontainer up --workspace-folder .
```

### B. Build Firmware
To build the project targeting the `src` directory:
```powershell
devcontainer exec --workspace-folder . idf.py -C src build
```

### C. Clean Build Artifacts
To perform an incremental clean:
```powershell
devcontainer exec --workspace-folder . idf.py -C src clean
```

> [!CAUTION]
> `fullclean` deletes the entire `src/build/` directory including cached CMake configurations.
> ```powershell
> devcontainer exec --workspace-folder . idf.py -C src fullclean
> ```

### D. Flash and Monitor
Flashing and serial monitoring require a target COM port. Confirm the serial port with the user before executing:
```powershell
# Flash firmware
devcontainer exec --workspace-folder . idf.py -C src -p <COM_PORT> flash

# Monitor serial output
devcontainer exec --workspace-folder . idf.py -C src -p <COM_PORT> monitor
```

---

## 2. Project Target & Components Architecture

- **Hardware Target**: ESP32-C6 (RISC-V 32-bit with IEEE 802.15.4 Zigbee / BLE).
- **Core Components**:
  - `src/main/`: Core application logic, FreeRTOS tasks, peripheral drivers, and Zigbee stack management.
  - `src/managed_components/`: Managed IDF components (e.g. `espressif__esp-zigbee-lib`). Handled via `idf_component.yml` and `dependencies.lock`.

### Common Pitfalls & Solutions

1. **Format Specifiers on 32-bit RISC-V**:
   Using `%d` for `uint32_t` causes `-Werror=format` compilation errors. Always include `<inttypes.h>` and use `PRIu32` / `PRId32` macros:
   ```c
   ESP_LOGI(TAG, "Value: %" PRIu32, value);
   ```

2. **Missing `PRIV_REQUIRES` in `src/main/CMakeLists.txt`**:
   If adding headers from an ESP-IDF or external component, make sure it is explicitly registered under `PRIV_REQUIRES` in `src/main/CMakeLists.txt`:
   ```cmake
   idf_component_register(SRCS ...
                          INCLUDE_DIRS "."
                          PRIV_REQUIRES esp_zigbee_lib esp_timer driver nvs_flash)
   ```
