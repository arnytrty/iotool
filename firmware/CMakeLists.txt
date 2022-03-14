cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(iotool)

pico_sdk_init()

add_executable(iotool
	main.c
)

target_link_libraries(iotool
	pico_stdlib
	hardware_pio
	hardware_dma
	hardware_irq
	hardware_clocks
)

pico_enable_stdio_usb(iotool 1)
pico_enable_stdio_uart(iotool 0)

pico_add_extra_outputs(iotool)
