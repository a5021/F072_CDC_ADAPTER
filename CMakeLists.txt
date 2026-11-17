cmake_minimum_required(VERSION 3.16)
project(f072_cdc_adapter VERSION 1.0.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# Pinned CMSIS revisions (no vendoring).
set(CMSIS_CORE_REV  "2b7495b8535bdcb306dac29b9ded4cfb679d7e5c")
set(CMSIS_F0_REV    "1ed05aa865aab1db2db2287077080aa44344fafa")

include(FetchContent)
FetchContent_Declare(cmsis_core
    GIT_REPOSITORY https://github.com/ARM-software/CMSIS_5.git
    GIT_TAG        ${CMSIS_CORE_REV})
FetchContent_Declare(cmsis_f0
    GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_device_f0.git
    GIT_TAG        ${CMSIS_F0_REV})
FetchContent_MakeAvailable(cmsis_core cmsis_f0)

set(CMSIS_CORE_INC  "${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include")
set(CMSIS_F0_INC    "${cmsis_f0_SOURCE_DIR}/Include")
set(CMSIS_F0_STARTUP "${cmsis_f0_SOURCE_DIR}/Source/Templates/gcc/startup_stm32f072xb.s")

set(DEFS
    STM32F072xB
    HSE_VALUE=8000000UL
)
set(OPT -mcpu=cortex-m0 -mthumb -Os -ffunction-sections -fdata-sections
        -fno-builtin -Wall -Wextra -Wno-unused-parameter -Werror)
add_compile_options(${OPT})

add_executable(f072_adapter
    firmware/src/main.c
    firmware/src/board.c
    firmware/src/timing.c
    firmware/src/i2c_master.c
    firmware/src/capture.c
    firmware/src/usb_cdc.c
    firmware/src/protocol.c
    firmware/src/script.c
    firmware/src/stats.c
    ${CMSIS_F0_STARTUP}
)
target_include_directories(f072_adapter PRIVATE
    firmware/include ${CMSIS_CORE_INC} ${CMSIS_F0_INC})
target_compile_definitions(f072_adapter PRIVATE ${DEFS})
target_link_options(f072_adapter PRIVATE
    -mcpu=cortex-m0 -mthumb --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections -Wl,-Map=${CMAKE_BINARY_DIR}/f072_adapter.map
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F072RB_FLASH.ld)
set_target_properties(f072_adapter PROPERTIES SUFFIX ".elf")

add_custom_command(TARGET f072_adapter POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:f072_adapter>
            ${CMAKE_BINARY_DIR}/f072_adapter.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:f072_adapter>
            ${CMAKE_BINARY_DIR}/f072_adapter.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:f072_adapter>
    COMMENT "Generating .bin/.hex and size report")
