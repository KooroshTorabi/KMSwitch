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
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# KMS_VERSION=x.y.z for releases ("dev" otherwise); KMS_SIGN_KEY=path to the private release key to make
# the signed .kmsfw bundles (kept offline by the maintainer; see tools/kmsfw.py)
if (NOT KMS_VERSION)
    set(KMS_VERSION "dev")
endif ()
set(TOOL ${CMAKE_CURRENT_LIST_DIR}/tools/kmsfw.py)
set(OUT ${CMAKE_CURRENT_BINARY_DIR})

# the release public key, built into the firmware (updates signed with another key are refused)
set(PUBKEY ${CMAKE_CURRENT_LIST_DIR}/keys/release.pub)
if (EXISTS ${PUBKEY})
    add_custom_command(OUTPUT ${OUT}/pubkey.c COMMAND ${Python3_EXECUTABLE} ${TOOL} pubc ${PUBKEY} ${OUT}/pubkey.c
                       DEPENDS ${PUBKEY} ${TOOL})
else ()
    message(WARNING "no keys/release.pub: this build accepts no update bundles (development build)")
    file(WRITE ${OUT}/pubkey.c "#include <stdint.h>\nconst uint8_t kms_release_pubkey[32] = {0};\n")
endif ()

# the settings page, gzip-compressed into flash (served by both MCUs)
add_custom_command(OUTPUT ${OUT}/page_gz.c
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/embed_page.py
            ${CMAKE_CURRENT_LIST_DIR}/../config/KMSWITCH.HTM ${OUT}/page_gz.c
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../config/KMSWITCH.HTM ${CMAKE_CURRENT_LIST_DIR}/tools/embed_page.py)

set(CORE
    src/core/kms_config.c src/core/kms_hotkey.c src/core/kms_layout.c src/core/kms_link.c
    src/core/kms_hidparse.c src/core/kms_typing.c src/core/kms_fw.c src/crypto/monocypher.c
    src/crypto/monocypher-ed25519.c)
set(COMMON src/usb_desc.c src/pc_port.c src/led.c src/link_io.c src/bootctl.c src/net/net.c src/net/http.c
    src/net/dhserver.c ${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c ${OUT}/page_gz.c ${OUT}/pubkey.c)
set(MCU_A src/app_a.c src/web_api.c src/update.c src/storage.c src/sd_spi.c src/diskio.c src/fatfs/ff.c
    src/fatfs/ffunicode.c)

# ------------------------------------------------------------------ bootloader (first 32 KB)
add_executable(kmswitch-boot boot/boot.c src/core/kms_fw.c)
target_include_directories(kmswitch-boot PRIVATE src)
target_compile_definitions(kmswitch-boot PRIVATE KMS_FW_NO_SIGNATURE=1 PICO_RUNTIME_NO_INIT_PER_CORE_TLS_SETUP=1)
target_link_libraries(kmswitch-boot PRIVATE pico_stdlib hardware_flash hardware_watchdog)
pico_set_linker_script(kmswitch-boot ${CMAKE_CURRENT_LIST_DIR}/ld/memmap_boot.ld)
pico_set_binary_type(kmswitch-boot default)
pico_add_bin_output(kmswitch-boot)

# ------------------------------------------------------------------ applications, linked for slot 0 and slot 1
function(kms_app board role slot)
    set(t kmswitch-${board}-${role}-s${slot})
    add_executable(${t} ${CORE} ${COMMON})
    target_include_directories(${t} PRIVATE src src/net ${PICO_TINYUSB_PATH}/lib/networking)
    target_compile_definitions(${t} PRIVATE KMS_BOARD='${board}' KMS_VERSION="${KMS_VERSION}")
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(${t} PRIVATE pico_stdlib pico_unique_id hardware_pio hardware_pwm hardware_uart
                          hardware_watchdog hardware_flash pico_multicore tinyusb_device tinyusb_board
                          pico_lwip_core pico_lwip_core4 pico_lwip_netif pico_lwip_arch)
    if (role STREQUAL "pc1")
        target_sources(${t} PRIVATE ${MCU_A})
        target_compile_definitions(${t} PRIVATE KMS_ROLE_A=1)
        target_link_libraries(${t} PRIVATE hardware_spi tinyusb_host tinyusb_pico_pio_usb)
    else ()
        target_sources(${t} PRIVATE src/app_b.c)
    endif ()
    pico_set_linker_script(${t} ${CMAKE_CURRENT_LIST_DIR}/ld/memmap_slot${slot}.ld)
    pico_add_bin_output(${t})
endfunction()

set(RELEASE_FILES)
foreach (board A B)
    foreach (role pc1 pc2)
        kms_app(${board} ${role} 0)
        kms_app(${board} ${role} 1)
        # BOOTSEL recovery image: boot2 + bootloader, the application in slot 0, an empty boot state
        set(uf2 ${OUT}/kmswitch-${board}-${role}-${KMS_VERSION}.uf2)
        add_custom_command(OUTPUT ${uf2}
            COMMAND ${Python3_EXECUTABLE} ${TOOL} uf2 ${uf2} 0x10000000=${OUT}/kmswitch-boot.bin
                    0x10008000=@ff:8192 0x1000A000=${OUT}/kmswitch-${board}-${role}-s0.bin
            DEPENDS kmswitch-boot kmswitch-${board}-${role}-s0 ${TOOL})
        list(APPEND RELEASE_FILES ${uf2})
    endforeach ()
    if (KMS_SIGN_KEY)
        set(b ${OUT}/kmswitch-${board}-${KMS_VERSION}.kmsfw)
        add_custom_command(OUTPUT ${b}
            COMMAND ${Python3_EXECUTABLE} ${TOOL} pack --board ${board} --version ${KMS_VERSION} --key ${KMS_SIGN_KEY}
                    -o ${b} pc1:0=${OUT}/kmswitch-${board}-pc1-s0.bin pc1:1=${OUT}/kmswitch-${board}-pc1-s1.bin
                    pc2:0=${OUT}/kmswitch-${board}-pc2-s0.bin pc2:1=${OUT}/kmswitch-${board}-pc2-s1.bin
            DEPENDS kmswitch-${board}-pc1-s0 kmswitch-${board}-pc1-s1 kmswitch-${board}-pc2-s0
                    kmswitch-${board}-pc2-s1 ${TOOL})
        list(APPEND RELEASE_FILES ${b})
    endif ()
endforeach ()
add_custom_target(release ALL DEPENDS ${RELEASE_FILES})
