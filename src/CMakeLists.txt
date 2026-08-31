cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

list(PREPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults")

if(DEFINED ENV{APP_VERSION})
    set(APP_VERSION $ENV{APP_VERSION})
else()
    set(APP_VERSION "0.0.1-dev")
endif()

if(DEFINED ENV{OTA_FILE_VERSION})
    set(OTA_FILE_VERSION $ENV{OTA_FILE_VERSION})
else()
    set(OTA_FILE_VERSION 0x00000100)
endif()

if(DEFINED ENV{BUILD_DATE})
    set(BUILD_DATE $ENV{BUILD_DATE})
else()
    string(TIMESTAMP BUILD_DATE "%Y%m%d")
endif()

set(PROJECT_VER ${APP_VERSION})

project(greenhouse_controller)

add_custom_command(TARGET gen_project_binary POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Generating Zigbee OTA Image with Version ${OTA_FILE_VERSION} (Build Date: ${BUILD_DATE})..."
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/image_builder_tool.py
        --manuf-id 0x1001
        --image-type 0x1011
        --file-version ${OTA_FILE_VERSION}
        --output ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}-v${APP_VERSION}.ota
        --tag 0x0000
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Building Zigbee OTA Image"
    VERBATIM
)
