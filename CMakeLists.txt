cmake_minimum_required(VERSION 3.28)

# Check if RPI is set
set(DEFAULT_RPI 4)
option(QEMU "Building for QEMU" OFF)
if(QEMU)
    message(WARNING "Building for QEMU")
    set(DEFAULT_RPI 3) # QEMU only supports up to Raspberry PI 3
endif()

if(NOT DEFINED RPI OR RPI EQUAL 0)
    message(WARNING "RPI environment variable is not defined, defaulting it to ${DEFAULT_RPI}: RPI=${DEFAULT_RPI}.")
    message(WARNING "To avoid any confusion, use 'cmake -DRPI=<version> ...' to set the actual Raspberry Pi version.")
    set(RPI ${DEFAULT_RPI} CACHE STRING "Raspberry Pi version" FORCE)
    set(ENV{RPI} ${RPI})
endif()

option(DEBUG_BUILD "Building with verbose information" OFF)
if(DEBUG_BUILD)
    message(WARNING "Running build with verbose information")
    set(CMAKE_VERBOSE_MAKEFILE ON)
    execute_process(
        COMMAND set -e
        COMMAND set -x
    )
endif()

include(${CMAKE_SOURCE_DIR}/cmake/helper.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/toolchain_setup.cmake)

project(MiniDexed)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CIRCLEVER "460000" CACHE STRING "Circle library version")

# Glob for source and header files of the Synth Dexed
file(GLOB_RECURSE SYNTHDEXED_SRC_C   "Synth_Dexed/src/*.c")
file(GLOB_RECURSE SYNTHDEXED_SRC_CPP "Synth_Dexed/src/*.cpp")
file(GLOB_RECURSE SYNTHDEXED_SRC_H   "Synth_Dexed/src/*.h")
file(GLOB_RECURSE SYNTHDEXED_SRC_HPP "Synth_Dexed/src/*.hpp")
list(REMOVE_ITEM  SYNTHDEXED_SRC_H   "${CMAKE_CURRENT_SOURCE_DIR}/Synth_Dexed/src/synth_dexed.h")
list(REMOVE_ITEM  SYNTHDEXED_SRC_CPP "${CMAKE_CURRENT_SOURCE_DIR}/Synth_Dexed/src/synth_dexed.cpp")

# Glob for source and header files of the MiniDexed
# file(GLOB_RECURSE MINIDEXED_SRC_C   "src/*.c") # C files are excluded
file(GLOB_RECURSE MINIDEXED_SRC_CPP "src/*.cpp")
file(GLOB_RECURSE MINIDEXED_SRC_H   "src/*.h")
file(GLOB_RECURSE MINIDEXED_SRC_HPP "src/*.hpp")

# Additional files to be included as source in the build
set(ADDITIONAL_SRC_FILES
    "${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/DSP/Source/SupportFunctions/SupportFunctions.c"
    "${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/DSP/Source/BasicMathFunctions/BasicMathFunctions.c"
    "${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/DSP/Source/FastMathFunctions/FastMathFunctions.c"
    "${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/DSP/Source/FilteringFunctions/FilteringFunctions.c"
    "${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/DSP/Source/CommonTables/CommonTables.c"
    "${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/DSP/ComputeLibrary/Source/arm_cl_tables.c"
)

# Update the dependencies
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/circle-stdlib/README.md")
    message(STATUS "Updating all dependencies...")
    execute_process(
        COMMAND bash ./submod.sh
        RESULT_VARIABLE result
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Failed to update dependencies.")
    endif()
endif()

# Copy the circle-stdlib to the build directory
if(NOT EXISTS "${CMAKE_BINARY_DIR}/circle-stdlib/README.md")
    message(STATUS "Copying circle-stdlib source code to ${CMAKE_BINARY_DIR}...")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/circle-stdlib")
    execute_process(COMMAND rsync -a --ignore-existing ${CMAKE_SOURCE_DIR}/circle-stdlib/ ${CMAKE_BINARY_DIR}/circle-stdlib/)
endif()

# Configure the circle-stdlib
if(NOT EXISTS "${CMAKE_BINARY_DIR}/circle-stdlib/Config.mk"
    OR NOT EXISTS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/Config.mk")
    message(STATUS "Configuring circle-stdlib...")
    # Define default system options as a list 
    set(CONFIG_OPTIONS_LIST 
        "-o USE_PWM_AUDIO_ON_ZERO" 
        "-o SAVE_VFP_REGS_ON_IRQ" 
        "-o REALTIME" 
        "-o SCREEN_DMA_BURST_LENGTH=1"
    )
    if(${RPI} GREATER 1)
        list(APPEND CONFIG_OPTIONS_LIST "-o ARM_ALLOW_MULTI_CORE")
    endif()
    if(DEFINED QEMU AND ${QEMU})
        list(APPEND CONFIG_OPTIONS_LIST "--qemu") 
    endif()

    # USB Vendor and Device ID for use with USB Gadget Mode 
    file(READ "${CMAKE_SOURCE_DIR}/USBID.sh" USBID_CONTENT) 
    string(REGEX MATCHALL "[^\n]+" LINES "${USBID_CONTENT}") 
    foreach(LINE IN LISTS LINES) 
        if(${LINE} MATCHES "USB_VID=(.+)") 
            set(USB_VID ${CMAKE_MATCH_1}) 
        elseif(${LINE} MATCHES "USB_DID=(.+)") 
            set(USB_DID ${CMAKE_MATCH_1}) 
        endif() 
    endforeach()
    if(NOT USB_VID) 
        message(WARNING "USB Vendor ID not set, defaulting to 0x1209")
        set(USB_VID "0x1209")
    endif()
    message(DEBUG "USB Vendor ID set to ${USB_VID}")
    list(APPEND CONFIG_OPTIONS_LIST "-o USB_GADGET_VENDOR_ID=${USB_VID}")
    
    if(NOT USB_DID)
        message(WARNING "USB Device ID not set, defaulting to 0xF043")
        set(USB_DID "0xF043")
    endif()
    message(DEBUG "USB Device ID set to ${USB_DID}")
    list(APPEND CONFIG_OPTIONS_LIST "-o USB_GADGET_DEVICE_ID_BASE=${USB_DID}")
    
    # Convert list to space-separated string for passing to environment 
    string(REPLACE ";" " " CONFIG_OPTIONS "${CONFIG_OPTIONS_LIST}")
    unset(CONFIG_OPTIONS_LIST)
    separate_arguments(CONFIG_OPTIONS_LIST UNIX_COMMAND ${CONFIG_OPTIONS})

    # Run the configure script
    execute_process(
        COMMAND ./configure -r ${RPI} --prefix ${TOOLCHAIN_PREFIX} ${CONFIG_OPTIONS_LIST} -o KERNEL_MAX_SIZE=0x400000
        RESULT_VARIABLE confguration_result
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/circle-stdlib"
    )
    if(NOT confguration_result EQUAL 0)
        message(FATAL_ERROR "Failed to configure circle-stdlib with error: ${confguration_result}")
    endif()
    unset(confguration_result)
endif()

if(NOT EXISTS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/Config.mk"
    OR NOT EXISTS "${CMAKE_BINARY_DIR}/circle-stdlib/Config.mk")
    message(FATAL_ERROR "Circle library is not properly configured")
else()
    message(STATUS "Setting configuration variables based on ${CMAKE_BINARY_DIR}/circle-stdlib/Config.mk...")
    load_key_value_file("${CMAKE_BINARY_DIR}/circle-stdlib/Config.mk")

    message(STATUS "Setting configuration variables based on ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/Config.mk...")
    load_key_value_file("${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/Config.mk")

    if(NOT DEFINED AARCH)
        message(FATAL_ERROR "AARCH is not defined in ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/Config.mk")
    endif()
endif()

set(TOOLCHAIN_LIB_PATH ${TOOLCHAIN_BIN_PATH}/../${TOOLCHAIN_NAME}/lib CACHE FILEPATH "Toolchain lib path" FORCE)

if(${AARCH} EQUAL 32)
    if(${RPI} EQUAL 1)
        set(TARGET "kernel" CACHE STRING "Target name for RPI 1 32 bits")
    elseif(${RPI EQUAL 2})
        set(TARGET "kernel7" CACHE STRING "Target name for RPI 2 32 bits")
    elseif(${RPI EQUAL 3})
        set(TARGET "kernel8-32" CACHE STRING "Target name for RPI 3 32 bits")
    elseif(${RPI EQUAL 4})
        set(TARGET "kernel7l" CACHE STRING "Target name for RPI 4 32 bits")
    else()
        message(FATAL_ERROR "For 32 bits platform, RPI must be set to 1, 2, 3 or 4")
    endif()
    set(LOADADDR 0x8000 CACHE STRING "Load address for 32 bits platforms" FORCE)
elseif(${AARCH} EQUAL 64)
    if(${RPI} EQUAL 3)
        set(TARGET "kernel8" CACHE STRING "Target name for RPI 3 64 bits")
    elseif(${RPI} EQUAL 4)
        set(TARGET "kernel8-rpi4" CACHE STRING "Target name for RPI 4 64 bits")
    elseif(${RPI} EQUAL 5)
        set(TARGET "kernel_2712" CACHE STRING "Target name for RPI 5 64 bits")
    else()
        message(FATAL_ERROR "For 64 bits platform, RPI must be set to 3, 4 or 5")
    endif()
    set(LOADADDR 0x80000 CACHE STRING "Load address for 64 bits platforms" FORCE)
else()
    message(FATAL_ERROR "AARCH must be set to 32 or 64")
endif()

add_custom_command(
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/Config.mk"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/Config.mk"

    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libc.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libcirclenewlib.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libg.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libm.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libnosys.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/nosys.specs"

    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/tools/converttool"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/libcircle.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/usb/libusb.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/usb/gadget/libusbgadget.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/input/libinput.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/fs/libfs.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/fs/fat/libfatfs.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/sched/libsched.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/net/libnet.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/sound/libsound.a"

    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/SDCard/libsdcard.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/fatfs/libfatfs.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/qemu/libqemusupport.a"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/wlan/libwlan.a"

    COMMAND . ${ENV_FILE} && set -e -x && make -j
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/circle-stdlib"
    COMMENT "Building circle-stdlib library..."
)

add_custom_command(
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/Config.mk"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/Config.mk"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libc.a"

    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/display/libdisplay.a"

    COMMAND . ${ENV_FILE} && set -e -x && make -j
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/display"
    COMMENT "Building circle-stdlib library addon display..."
)

add_custom_command(
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/Config.mk"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/Config.mk"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libc.a"

    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/sensor/libsensor.a"

    COMMAND . ${ENV_FILE} && set -e -x && make -j
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/sensor"
    COMMENT "Building circle-stdlib library addon sensor..."
)

add_custom_command(
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/Config.mk"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/Config.mk"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libc.a"

    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/Properties/libproperties.a"

    COMMAND . ${ENV_FILE} && set -e -x && make -j
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/Properties"
    COMMENT "Building circle-stdlib library addon properties..."
)

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/LICENCE.broadcom"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/COPYING.linux"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bootcode.bin"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/start.elf"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/fixup.dat"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/start4.elf"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/fixup4.dat"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/start_cd.elf"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/fixup_cd.dat"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/start4cd.elf"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/fixup4cd.dat"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2710-rpi-zero-2-w.dtb"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2711-rpi-4-b.dtb"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2711-rpi-400.dtb"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2711-rpi-cm4.dtb"
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2712-rpi-5-b.dtb"

    COMMAND . ${ENV_FILE} && set -e -x && make -j
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot"
    COMMENT "Downloading boot loaders"
)

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/armstub8-rpi4.bin"

    COMMAND . ${ENV_FILE} && set -e -x && make armstub64
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot"
    COMMENT "Downloading boot loaders"
)

add_custom_target(
    Circle

    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libc.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libcirclenewlib.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libg.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libm.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libnosys.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/nosys.specs"

    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/tools/converttool"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/libcircle.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/usb/libusb.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/usb/gadget/libusbgadget.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/input/libinput.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/fs/libfs.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/fs/fat/libfatfs.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/sched/libsched.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/net/libnet.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/sound/libsound.a"

    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/SDCard/libsdcard.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/fatfs/libfatfs.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/qemu/libqemusupport.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/wlan/libwlan.a"
    
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/display/libdisplay.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/sensor/libsensor.a"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/Properties/libproperties.a"

    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/LICENCE.broadcom"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/COPYING.linux"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bootcode.bin"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/start.elf"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/fixup.dat"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/start4.elf"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/fixup4.dat"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/start_cd.elf"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/fixup_cd.dat"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/start4cd.elf"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/fixup4cd.dat"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2710-rpi-zero-2-w.dtb"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2711-rpi-4-b.dtb"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2711-rpi-400.dtb"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2711-rpi-cm4.dtb"
    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/bcm2712-rpi-5-b.dtb"

    DEPENDS "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot/armstub8-rpi4.bin"

    COMMENT "Circle-stdlib library and addons are built"
)

add_executable(
    MiniDexed
    ${SYNTHDEXED_SRC_C}
    ${SYNTHDEXED_SRC_CPP}
    ${SYNTHDEXED_SRC_H}
    ${SYNTHDEXED_SRC_HPP}
    ${MINIDEXED_SRC_C}
    ${MINIDEXED_SRC_CPP}
    ${MINIDEXED_SRC_H}
    ${MINIDEXED_SRC_HPP}
    ${ADDITIONAL_SRC_FILES}
)

add_dependencies(
    MiniDexed 
    Circle
)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/Synth_Dexed/src
    ${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/Core/Include
    ${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/DSP/Include
    ${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/DSP/PrivateInclude
    ${CMAKE_SOURCE_DIR}/CMSIS_5/CMSIS/DSP/ComputeLibrary/Include
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/include
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/app/lib
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/vc4
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/vc4/interface/khronos/include
    ${CMAKE_BINARY_DIR}/circle-stdlib/include
    ${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/include    
)

target_compile_definitions(
    MiniDexed
    PRIVATE
    USE_FX
    __circle__=${CIRCLEVER}
    RASPPI=${RASPPI}
    STDLIB_SUPPORT=${STDLIB_SUPPORT}
    __VCCOREVER__=0x04000000
)

separate_arguments(ARCH_LIST UNIX_COMMAND ${ARCH})
separate_arguments(DEFINE_LIST UNIX_COMMAND ${DEFINE})

target_compile_options(
    MiniDexed 
    PRIVATE 
    ${CFLAGS}
    ${ARCH_LIST}
    ${DEFINE_LIST}
    -Wall
    -fsigned-char
    -g
    -U__unix__
    -U__linux__
    -O3
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-aligned-new>
)

set_target_properties(
    MiniDexed
    PROPERTIES
    OUTPUT_NAME ${TARGET}
    SUFFIX .elf
)

target_link_options(
    MiniDexed 
    PRIVATE 
    -Map ${TARGET}.map
    --section-start=.init=${LOADADDR}
    -T ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/circle.ld
)

set(GROUPED_LIBS
    ${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libm.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libc.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/install/${NEWLIB_ARCH}/lib/libcirclenewlib.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/display/libdisplay.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/sensor/libsensor.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/Properties/libproperties.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/SDCard/libsdcard.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/usb/libusb.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/usb/gadget/libusbgadget.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/input/libinput.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/sound/libsound.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/addon/fatfs/libfatfs.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/fs/libfs.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/sched/libsched.a
    ${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/lib/libcircle.a
    ${TOOLCHAIN_LIB_PATH}/libstdc++.a
    ${STDDEF_INCPATH}/../libgcc.a
)

target_link_libraries(
    MiniDexed
    PRIVATE
    "--start-group"
    ${GROUPED_LIBS}
    "--end-group"
)

# Add post-build commands to generate lst and img files
add_custom_command(
    DEPENDS ${TARGET}.elf

    OUTPUT ${TARGET}.lst
    OUTPUT ${TARGET}.img
   
    # Generate lst file
    COMMAND ${CMAKE_COMMAND} -E echo "  DUMP  ${TARGET}.lst"
    COMMAND ${CMAKE_OBJDUMP} -d ${TARGET}.elf | ${CMAKE_CXXFILT} > ${TARGET}.lst
    
    # Generate img file
    COMMAND ${CMAKE_COMMAND} -E echo "  COPY  ${TARGET}.img"
    COMMAND ${CMAKE_OBJCOPY} ${TARGET}.elf -O binary ${TARGET}.img
    
    # Display file size
    COMMAND ${CMAKE_COMMAND} -E echo_append "  SIZE  ${TARGET}.img => "
    COMMAND wc -c < ${TARGET}.img
    
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    VERBATIM

    COMMENT "Preparing kernel image"
)

# Add a custom target that depends on the generated files
add_custom_target(MiniDexedKernelImage ALL
    DEPENDS ${TARGET}.lst ${TARGET}.img
)

add_dependencies(MiniDexedKernelImage MiniDexed)

# Prepare the sdcard installation
add_custom_command(
    TARGET MiniDexedKernelImage
    POST_BUILD
    
    # Create sdcard directory
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/sdcard"
    
    # Copy boot files
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        "${CMAKE_BINARY_DIR}/circle-stdlib/libs/circle/boot" 
        "${CMAKE_BINARY_DIR}/sdcard"
    
    # Remove unwanted files
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CMAKE_BINARY_DIR}/sdcard/config*.txt"
        "${CMAKE_BINARY_DIR}/sdcard/README"
        "${CMAKE_BINARY_DIR}/sdcard/Makefile"
        "${CMAKE_BINARY_DIR}/sdcard/COPYING.linux"
        "${CMAKE_BINARY_DIR}/sdcard/armstub"

    # Copy required files
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/config.txt"
        "${CMAKE_SOURCE_DIR}/src/minidexed.ini"
        "${CMAKE_BINARY_DIR}/${TARGET}.img"
        "${CMAKE_SOURCE_DIR}/src/performance.ini"
        "${CMAKE_SOURCE_DIR}/getsysex.sh"
        "${CMAKE_BINARY_DIR}/sdcard/"
    
    # Create cmdline.txt
    COMMAND ${CMAKE_COMMAND} -E echo "usbspeed=full" > "${CMAKE_BINARY_DIR}/sdcard/cmdline.txt"
    
    # Copy the kernels
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/kernel*.img"
        "${CMAKE_BINARY_DIR}/sdcard"

    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Preparing the sdcard image"
)

# Install presets and performances
add_custom_command(
    TARGET MiniDexedKernelImage
    POST_BUILD

    # Create sounds directory
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/sounds"
    
    # Clone repository only if it doesn't exist
    COMMAND ${CMAKE_COMMAND} -E echo "Checking for sounds..."
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/prepare_sounds.cmake
    
    # Copy files
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/sounds/Soundplantage/performance"
        "${CMAKE_BINARY_DIR}/sdcard/performance"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/sounds/Soundplantage/*.pdf"
        "${CMAKE_BINARY_DIR}/sdcard"

    COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_SOURCE_DIR}/cmake/prepareFS.sh ${CMAKE_BINARY_DIR} sdcard ${RPI} ${TARGET} ${QEMU}

    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Installing presets and performances"
)
