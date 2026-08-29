---
name: esp-idf-build
description: Build this ESP-IDF project. Use when the user asks to build, compile, flash, monitor, or clean the project. Covers the working build command and environment setup.
---

# ESP-IDF Build

This skill documents how to build and manage this ESP-IDF project from Claude Code.

## Overview

An ESP-IDF project can be seen as an amalgamation of a number of components. For example, for a web server that shows the current humidity, there could be:

- The ESP-IDF base libraries (libc, ROM bindings, etc)

- The Wi-Fi drivers

- A TCP/IP stack

- The FreeRTOS operating system

- A web server

- A driver for the humidity sensor

- Main code tying it all together

ESP-IDF makes these components explicit and configurable. To do that, when a project is compiled, the build system will look up all the components in the ESP-IDF directories, the project directories and (optionally) in additional custom component directories. It then allows the user to configure the ESP-IDF project using a text-based menu system to customize each component. After the components in the project are configured, the build system will compile the project.

## Environment

All paths are derived from the variables below. Update them when IDF version or install location changes.

| Variable | Current Value | How to Find |
|----------|---------------|-------------|
| `IDF_PATH` | `/opt/esp/idf` | `echo $env:IDF_PATH` |
| `IDF_TOOLS_PATH` | `/opt/esp` | default unless customized |
| `IDF_PYTHON_ENV_PATH` | `/opt/esp/python_env/idf<VERSION>_py3.12_env` | `ls /opt/esp/python_env` |

## Project structure example

```
- myProject/
    - CMakeLists.txt
    - sdkconfig
    - dependencies.lock
    - bootloader_components/ - boot_component/ - CMakeLists.txt
                                            - Kconfig
                                            - src1.c
    - components/ - component1/ - CMakeLists.txt
                                - Kconfig
                                - src1.c
                - component2/ - CMakeLists.txt
                                - Kconfig
                                - src1.c
                                - include/ - component2.h
    - managed_components/ - namespace__component-name/ - CMakelists.txt
                                                    - src1.c
                                                    - idf_component.yml
                                                    - include/ - src1.h
    - main/       - CMakeLists.txt
                - src1.c
                - src2.c
                - idf_component.yml
    - build/
```

This example "myProject" contains the following elements:

A top-level project CMakeLists.txt file. This is the primary file which CMake uses to learn how to build the project; and may set project-wide CMake variables. It includes the file /tools/cmake/project.cmake which implements the rest of the build system. Finally, it sets the project name and defines the project.

"sdkconfig" project configuration file. This file is created/updated when idf.py menuconfig runs, and holds the configuration for all of the components in the project (including ESP-IDF itself). The sdkconfig file may or may not be added to the source control system of the project. More information about this file can be found in the sdkconfig file section in the Configuration Guide.

"dependencies.lock" file contains the list of all managed components, and their versions, that are currently in used in the project. The dependencies.lock file is generated or updated automatically when IDF Component Manager is used to add or update project components. So this file should never be edited manually! If the project does not have idf_component.yml files in any of its components, dependencies.lock will not be created.

Optional "idf_component.yml" file contains metadata about the component and its dependencies. It is used by the IDF Component Manager to download and resolve these dependencies. More information about this file can be found in the idf_component.yml section.

Optional "bootloader_components" directory contains components that need to be compiled and linked inside the bootloader project. A project does not have to contain custom bootloader components of this kind, but it can be useful in case the bootloader needs to be modified to embed new features.

Optional "components" directory contains components that are part of the project. A project does not have to contain custom components of this kind, but it can be useful for structuring reusable code or including third-party components that aren't part of ESP-IDF. Alternatively, EXTRA_COMPONENT_DIRS can be set in the top-level CMakeLists.txt to look for components in other places.

"main" directory is a special component that contains source code for the project itself. "main" is a default name, the CMake variable COMPONENT_DIRS includes this component but you can modify this variable. See the renaming main section for more info. If you have a lot of source files in your project, we recommend grouping most into components instead of putting them all in "main".

"build" directory is where the build output is created. This directory is created by idf.py if it doesn't already exist. CMake configures the project and generates interim build files in this directory. Then, after the main build process is run, this directory will also contain interim object files and libraries as well as final binary output files. This directory is usually not added to source control or distributed with the project source code.

"managed_components" directory is created by the IDF Component Manager to store components managed by this tool. Each managed component typically includes a idf_component.yml manifest file defining the component's metadata, such as version and dependencies. However, for components sourced from Git repositories, the manifest file is optional. Users should avoid manually modifying the contents of the "managed_components" directory. If alterations are needed, the component can be copied to the components directory. The "managed_components" directory is usually not versioned in Git and not distributed with the project source code.

Component directories each contain a component CMakeLists.txt file. This file contains variable definitions to control the build process of the component, and its integration into the overall project. See Component CMakeLists Files for more details.

Each component may also include a Kconfig file defining the component configuration options that can be set via menuconfig. Some components may also include Kconfig.projbuild and project_include.cmake files, which are special files for overriding parts of the project.

## Component Management

ESP-IDF supports two ways to include external components:

### Option A: Local `components/` directory (recommended)

Copy or symlink the component into `<project>/components/<name>/`. ESP-IDF automatically discovers it. No `idf_component.yml` entry needed for the copied component.

```
my_project/
├── components/
│   └── esp-tflite-micro/   # copied here
└── main/
    ├── CMakeLists.txt       # PRIV_REQUIRES must list the component name
    └── idf_component.yml    # only declare dependencies NOT in components/
```

### Option B: `override_path` in idf_component.yml

```yaml
dependencies:
  espressif/esp-tflite-micro:
    version: "*"
    override_path: "../../../path/to/component"
```

This can fail in ESP-IDF v6.0.1 if the component manager cannot resolve the path. Prefer Option A when in doubt.

### Common Pitfall: missing PRIV_REQUIRES

If `main` includes headers from a component, that component must be in `PRIV_REQUIRES`:

```cmake
idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES spi_flash esp-tflite-micro  # <-- required
)
```

Without this, the compiler reports `No such file or directory` for the component's headers.

## Common Tasks

### Build
```bash
# ... (use build command above)
```

### Clean

⚠️ **Destructive operation.** `fullclean` deletes the entire `build/` directory. Confirm with the user before running.

Use the same command block as Build, replacing `build` with `fullclean`.

### Flash

Flash requires a COM port. Ask the user for the port before flashing.

Use the same command block as Build, replacing `build` with `-p COM_PORT flash`.

### Monitor

Use the same command block as Build, replacing `build` with `-p COM_PORT monitor`.

## Build Output

- **Firmware**: `build/<project_name>.bin`
- **Bootloader**: `build/bootloader/bootloader.bin`
- **Partition table**: `build/partition_table/partition-table.bin`

## Common Pitfalls

### Format specifiers on ESP32-S3

On ESP32-S3, `int32_t` is `long int` and `uint32_t` is `long unsigned int`. Using `%d` for these types causes `-Werror=format` build failures. Use `PRI` macros from `<cinttypes>`:

```cpp
#include <cinttypes>
printf("zero_point=%" PRId32 "\n", input->params.zero_point);  // correct
printf("version=%" PRIu32 "\n", model->version());             // correct
```

## Known Warnings (non-fatal)

- `ESP_ROM_ELF_DIR not defined` — only affects gdbinit generation, does not affect build
- `Running as elevated user` — eim warning, safe to ignore
