# ESP32-C6 & FreeRTOS Development Rules

## 1. Concurrency & FreeRTOS Task Architecture
- **Task Stack Sizing**: Always allocate sufficient stack size based on task requirements (minimum 2048 to 4096 bytes for sensor/communication tasks, higher if deep call stacks or printf formatting are used).
- **Queue-based IPC**: Pass sensor readings, state updates, and Zigbee report triggers via FreeRTOS Queues (`QueueHandle_t`) or RingBuffers rather than sharing raw global variables without synchronization.
- **Task Delays**: Use `vTaskDelay(pdMS_TO_TICKS(ms))` or `vTaskDelayUntil()` for periodic polling loops. **Never** use busy-wait loops (`esp_rom_delay_us` or `while(1)`) for periodic scheduling.
- **Mutexes & Critical Sections**: Guard shared hardware buses (e.g., I2C bus accessed by multiple tasks) with FreeRTOS Mutexes (`SemaphoreHandle_t`). Always use timeouts (e.g., `pdMS_TO_TICKS(100)`) instead of `portMAX_DELAY` to prevent deadlock.

## 2. Error Handling & Robustness
- Use `esp_err_t` return types for all driver functions.
- For non-critical runtime failures (e.g., transient I2C read failure or UART checksum error), log a warning with `ESP_LOGW` and implement exponential backoff retry. **Do not** trigger system aborts (`ESP_ERROR_CHECK`) on runtime peripheral timeouts.
- Reserve `ESP_ERROR_CHECK` strictly for one-time boot-phase initializations (NVS init, Zigbee stack registration, GPIO mode setup).

## 3. Formatting & Types on 32-bit Targets
- On 32-bit architectures (RISC-V ESP32-C6):
  - Use `int32_t`, `uint32_t`, `int16_t`, `uint16_t`, `int8_t`, `uint8_t` for explicit width guarantees.
  - For formatted printing (`printf`, `ESP_LOGI`), always use `<inttypes.h>` macros (`PRIu32`, `PRId32`, `PRIx32`, `PRIu16`, etc.) to prevent `-Werror=format` compilation failures.

## 4. Hardware Pin Mapping & Kconfig
- Define GPIO pin assignments in header constants or configurable Kconfig options (`src/main/Kconfig`), avoiding hardcoded magic numbers across multiple `.c` files.
- Ensure GPIO configuration sets appropriate pull-up/pull-down resistors and open-drain modes for I2C buses.
