cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

set(projname "anyoperate_target")
project(${projname})

pico_sdk_init()

add_executable(${projname}
    main.cpp
    config_protocol.cpp
    ble_transport.cpp
    wifi_tcp_transport.cpp
    wifi_manager.cpp
    flash_volume_store.cpp
    persistent_store.cpp
)

target_include_directories(${projname} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(${projname}
    pico_stdlib
    hardware_clocks
    hardware_flash
    hardware_watchdog
    pico_flash
)

if (PICO_CYW43_SUPPORTED)

    target_compile_definitions(${projname}
        PRIVATE
        HAS_WLAN_MODULE=1
        PICO_CYW43_ARCH_DEBUG_ENABLED=0
    )

    target_link_libraries(${projname}
        pico_cyw43_arch_lwip_poll
        pico_btstack_ble
        pico_btstack_cyw43
    )

    pico_btstack_make_gatt_header(
        ${projname}
        PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/easyconfig.gatt
    )

else()

    target_compile_definitions(${projname}
        PRIVATE
        HAS_WLAN_MODULE=0
    )

endif()

pico_enable_stdio_usb(${projname} 1)
pico_enable_stdio_uart(${projname} 0)

pico_add_extra_outputs(${projname})
