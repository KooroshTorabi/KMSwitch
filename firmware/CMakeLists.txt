cmake_minimum_required(VERSION 3.13)

# Dependencies (see get_deps.sh): Raspberry Pi pico-sdk 2.1.1 with its TinyUSB and lwIP submodules,
# and Pico-PIO-USB at the commit TinyUSB was tested with.
if (NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/deps/pico-sdk)
endif ()
if (NOT PICO_PIO_USB_PATH)
    if (DEFINED ENV{PICO_PIO_USB_PATH})
        set(PICO_PIO_USB_PATH $ENV{PICO_PIO_USB_PATH})
    else ()
        set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/deps/Pico-PIO-USB)
    endif ()
endif ()
set(PICO_BOARD pico CACHE STRING "")   # generic RP2040 with 2 MB QSPI flash (W25Q16, as on both boards)

include(pico_sdk_import.cmake)
project(kmswitch C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

if (NOT KMS_VERSION)
    set(KMS_VERSION "dev")
endif ()

set(CORE
    src/core/kms_config.c src/core/kms_hotkey.c src/core/kms_layout.c src/core/kms_link.c
    src/core/kms_hidparse.c src/core/kms_typing.c)
set(COMMON src/usb_desc.c src/pc_port.c src/led.c src/link_io.c)
set(MCU_A src/app_a.c src/storage.c src/sd_spi.c src/diskio.c src/fatfs/ff.c src/fatfs/ffunicode.c)

function(kms_target board role)
    set(t kmswitch-${board}-${role})
    add_executable(${t} ${CORE} ${COMMON})
    target_include_directories(${t} PRIVATE src)
    target_compile_definitions(${t} PRIVATE KMS_BOARD='${board}' KMS_VERSION="${KMS_VERSION}")
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(${t} PRIVATE pico_stdlib pico_unique_id hardware_pio hardware_pwm hardware_uart
                          hardware_watchdog tinyusb_device tinyusb_board)
    if (role STREQUAL "pc1")
        target_sources(${t} PRIVATE ${MCU_A})
        target_compile_definitions(${t} PRIVATE KMS_ROLE_A=1)
        target_link_libraries(${t} PRIVATE pico_multicore hardware_spi tinyusb_host tinyusb_pico_pio_usb)
        set_source_files_properties(src/fatfs/ff.c PROPERTIES COMPILE_FLAGS "-Wno-unused-function")
    else ()
        target_sources(${t} PRIVATE src/app_b.c)
    endif ()
    pico_add_extra_outputs(${t})
endfunction()

foreach (board A B)
    kms_target(${board} pc1)
    kms_target(${board} pc2)
endforeach ()
