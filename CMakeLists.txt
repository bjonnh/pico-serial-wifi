cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_udp C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_CYW43_ARCH_POLL 0)
set(DEBUG 0)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
set(PICO_BOARD pico_w)
pico_sdk_init()

add_executable(udp_server
        server.c
    dhcpserver/dhcpserver.c
    dnsserver/dnsserver.c
)

target_include_directories(udp_server PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/dhcpserver
        ${CMAKE_CURRENT_LIST_DIR}/dnsserver
)

add_executable(udp_client
        client.c
)


pico_configure_ip4_address(udp_server PRIVATE
        CYW43_DEFAULT_IP_AP_ADDRESS 192.168.4.1
)


target_include_directories(udp_client PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)



target_link_libraries(udp_server
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
)

target_link_libraries(udp_client
    pico_stdlib
    pico_cyw43_arch_lwip_poll
)

pico_enable_stdio_usb(udp_server 1)
pico_enable_stdio_uart(udp_server 0)
pico_enable_stdio_usb(udp_client 1)
pico_enable_stdio_uart(udp_client 0)

pico_add_extra_outputs(udp_server)
pico_add_extra_outputs(udp_client)
