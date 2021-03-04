cmake_minimum_required(VERSION 3.13)

# initialize pico-sdk from submodule
# note: this must happen before project()
include(../../contrib/pico-sdk/pico_sdk_init.cmake)

project(migic)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

include_directories(
    src/
    ../../inc
    ../../contrib/libfixmath
)

FILE(GLOB LIBMIGIC_SOURCE ../../src/*.c)
add_definitions(
    -DMIGIC_NO_64_BIT
    -DFIXMATH_NO_OVERFLOW
    )
# rest of your project
add_executable(migic
    # libmigic source
    ${LIBMIGIC_SOURCE}
    src/usb_descriptors.c
    src/main.c
)
# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(migic pico_stdlib pico_multicore hardware_adc hardware_dma tinyusb_device tinyusb_board)


# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(migic)

# Generate from build with: cmake -LA .. -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG"
