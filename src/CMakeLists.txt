cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

list(PREPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults")

if(DEFINED ENV{APP_VERSION})
    set(APP_VERSION $ENV{APP_VERSION})
else()
    set(APP_VERSION "1.0.0-dev")
endif()

if(DEFINED ENV{OTA_FILE_VERSION})
    set(OTA_FILE_VERSION $ENV{OTA_FILE_VERSION})
else()
    set(OTA_FILE_VERSION 0x01010110)
endif()

project(greenhouse_controller)

add_custom_command(TARGET gen_project_binary POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Generating Zigbee OTA Image with Version ${OTA_FILE_VERSION}..."
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/image_builder_tool.py
        --manuf-id 0x1001
        --image-type 0x1011
        --file-version ${OTA_FILE_VERSION}
        --output ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}-v${APP_VERSION}.ota
        --tag 0x1234
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Building Zigbee OTA Image"
    VERBATIM
)
