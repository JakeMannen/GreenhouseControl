
# Project main features
This is a ESP-IDF embedded system project for a greenhouse irrigation controller. Main features and technologies are Zigbee, I2C, UART and GPIO that communicates with devices & sensors such as water pumps, temperature/humidity, On/Off switches and Solar panel controller.

## Device
- The ESP32-C6 is the intended hardware for this controller software
- Make changes also compatible with other hardware when poosible

## Frameworks

- Use these frameworks unless new functionalty requires upgrade
    - ESP-IDF >= v6.1
    - esp_zigbee_lib >= v2.0.1

- NEVER install anything on the host machine like python, esp-idf or other tools
- ALWAYS ask before upgrading or downgrading framework versions

## Coding style
- Use common style guides for ESP-IDF and FreeRTOS

## Tesing
- Add tests for new introduced or changed code when possible

## Documentation
- The `README.md` must ALWAYS be updated whenever functions, hardware configurations, endpoints, commands, or any user-facing functionality are introduced or modified.

## Git (GitFlow Model)
- Repository strictly uses a simplified **GitFlow** branching strategy with only **`main`**, **`dev`**, and **`feature/*`** (or **`feat/*`**) branches.
- **NEVER** push changes directly to **`main`** or **`dev`** branches.
- **Feature flow**:
  - Always fetch the latest changes from `dev` before creating a feature branch.
  - Create a new feature branch named `feat/<descriptive_branch_name>` originating from `dev`.
  - Feature PRs must target `dev` (enforced by CI branch validation).
- **Release flow**:
  - Releases are made by opening a PR from `dev` targeting `main`.
  - Merging `dev` into `main` triggers automated semantic release and Zigbee OTA firmware artifact generation.
- **Pull Requests & Commits**:
  - Use short descriptive PR titles following Conventional Commits format (e.g., `feat(sensor): ...`, `fix(ota): ...`), enforced by the PR Linter.
  - Run tests and verify the project builds (`idf.py -C src build`) before committing changes or opening a PR.

## Executing commands
- When esp-idf commands like "idf.py build" or "idf.py menuconfig" needs to be used, if the dev container is not running start it by executing:
`devcontainer up --workspace-folder .`.

- And execute commands targeting the `src` directory:
    - `devcontainer exec --workspace-folder . idf.py -C src build`
    - `devcontainer exec --workspace-folder . idf.py -C src flash`

## Resources
- ESP-IDF source code: https://github.com/espressif/esp-idf
- esp_zigbee_lib source code: https://github.com/espressif/esp-zigbee-sdk/tree/main/components/esp-zigbee-lib
- esp_zigbee_lib migration from v1.X to v2.X: https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/migration-guide/v2.x/index.html