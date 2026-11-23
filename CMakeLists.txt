cmake_minimum_required(VERSION 3.13)

# Cần Pico SDK >= 2.1 để có sẵn board waveshare_rp2350_zero.
# SDK cũ hơn: đổi thành pico2.
set(PICO_BOARD waveshare_rp2350_zero)

# Chép pico_sdk_import.cmake từ $PICO_SDK_PATH/external vào thư mục này
include(pico_sdk_import.cmake)

project(numpad30 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(numpad30
    main.c
    ssd1306.c
    config.c
    audio.c
    air.c
    midi.c
    player.c
    game.c
    pet.c
    usb_descriptors.c
)

pico_generate_pio_header(numpad30 ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)

target_include_directories(numpad30 PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(numpad30
    pico_stdlib
    hardware_i2c
    hardware_flash
    pico_flash
    hardware_pio
    hardware_dma
    tinyusb_device
)

pico_add_extra_outputs(numpad30)
