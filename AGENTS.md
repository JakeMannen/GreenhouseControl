# Project Main Features
This is an ESP-IDF embedded system project for a greenhouse irrigation controller. Main features and technologies include Zigbee 3.0, I2C, UART, and GPIO communicating with peripherals and sensors such as water pumps, temperature/humidity sensors (SHT30), On/Off load switches, and a Victron solar charge controller (VE.Direct).

## Target Hardware & Compatibility
- **Primary Hardware**: ESP32-C6 (intended target).
- Maintain compatibility with other ESP32 series hardware where applicable.

## Frameworks & Host Isolation
- **Framework Versions**:
  - ESP-IDF $\ge$ `v6.1`
  - esp_zigbee_lib $\ge$ `v2.0.1`
- **Host Machine Protection**: **NEVER** install anything on the host machine (such as Python, ESP-IDF, toolchains, or local packages). Always operate through the devcontainer.
- **Framework Upgrades**: **ALWAYS** ask for user confirmation before upgrading or downgrading framework versions.

## Coding Style & Architecture
- Follow common style guides for ESP-IDF and FreeRTOS.
- Keep task priorities, stack sizes, and FreeRTOS queue pipelines modular and well-documented.
- Detailed conventions: [.agents/rules/esp32c6-freertos.md](file:///.agents/rules/esp32c6-freertos.md)

## Zigbee Protocol Standards
- Follow standard Zigbee Cluster Library (ZCL) specifications for clusters, endpoints, and attribute reporting.
- Keep `Zigbee2Mqtt/external_converters/greenhouse_controller.js` in exact sync with firmware endpoints and cluster reporting configurations.
- Detailed conventions: [.agents/rules/zigbee-standards.md](file:///.agents/rules/zigbee-standards.md)

## Testing & Quality Assurance
- Add unit tests for newly introduced or modified code whenever possible (using Unity / ESP-IDF test framework).
- Always verify that the project builds cleanly before committing.

## Documentation
- The [README.md](file:///README.md) **MUST ALWAYS** be updated whenever functions, hardware pinouts, endpoints, commands, or user-facing configurations are introduced or modified.

## Git Workflow (GitFlow Model)
- Repository strictly uses a simplified **GitFlow** branching strategy with only **`main`**, **`dev`**, and **`feature/*`** (or **`feat/*`**) branches.
- **NEVER** push changes directly to **`main`** or **`dev`** branches.
- **Feature Flow**:
  - Always fetch the latest changes from `dev` before creating a feature branch.
  - Create a new feature branch named `feat/<descriptive_branch_name>` originating from `dev`.
  - Feature PRs must target `dev` (enforced by CI branch validation).
- **Release Flow**:
  - Releases are made by opening a PR from `dev` targeting `main`.
  - Merging `dev` into `main` triggers automated semantic release and Zigbee OTA firmware artifact generation.
- **Pull Requests & Commits**:
  - Use short descriptive PR titles following Conventional Commits format (e.g., `feat(sensor): ...`, `fix(ota): ...`), enforced by the PR Linter.
- Detailed conventions: [.agents/rules/gitflow-workflow.md](file:///.agents/rules/gitflow-workflow.md)

## Command Execution (Devcontainer)
- When ESP-IDF commands (`idf.py build`, `idf.py menuconfig`, etc.) are needed:
  1. Start the devcontainer if not running:
     ```powershell
     devcontainer up --workspace-folder .
     ```
  2. Execute commands targeting the `src` directory:
     ```powershell
     devcontainer exec --workspace-folder . idf.py -C src build
     devcontainer exec --workspace-folder . idf.py -C src flash
     ```

## Specialized Subagent Roles
For complex, multi-component development tasks, specialized subagents can be invoked:
- **Firmware Architect**: Concurrency, memory layout, and FreeRTOS task budgets ([`firmware-architect.md`](file:///.agents/subagents/firmware-architect.md)).
- **Zigbee Specialist**: ZCL clusters, endpoint routing, and Zigbee2MQTT sync ([`zigbee-specialist.md`](file:///.agents/subagents/zigbee-specialist.md)).
- **Driver Engineer**: I2C, UART (VE.Direct), and GPIO peripherals ([`sensor-driver-engineer.md`](file:///.agents/subagents/sensor-driver-engineer.md)).
- **QA & Test Engineer**: Unity test suites and build verification ([`qa-test-engineer.md`](file:///.agents/subagents/qa-test-engineer.md)).
- Detailed guide: [.agents/subagents/README.md](file:///.agents/subagents/README.md)

## Resources & References
- ESP-IDF Source Code & Docs: https://github.com/espressif/esp-idf
- esp_zigbee_lib Source Code: https://github.com/espressif/esp-zigbee-sdk/tree/main/components/esp-zigbee-lib
- esp_zigbee_lib v1.x $\rightarrow$ v2.x Migration Guide: https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/migration-guide/v2.x/index.html