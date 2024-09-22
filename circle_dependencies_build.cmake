# Define system options for the circle library
set(CIRCLE_OPTIONS "-o USE_PWM_AUDIO_ON_ZERO -o SAVE_VFP_REGS_ON_IRQ -o REALTIME -o SCREEN_DMA_BURST_LENGTH=1")
if($ENV{RPI} GREATER 1)
    set(CIRCLE_OPTIONS "${CIRCLE_OPTIONS} -o ARM_ALLOW_MULTI_CORE")
endif()

# Source USBID.sh and capture output
execute_process(
    COMMAND bash -c "source ${CMAKE_SOURCE_DIR}/USBID.sh && echo USB_VID=\$USB_VID && echo USB_DID=\$USB_DID"
    OUTPUT_VARIABLE USBID_OUTPUT
)

# Parse USBID.sh output
string(REGEX MATCH "USB_VID=([^\n]+)" _ ${USBID_OUTPUT})
set(USB_VID ${CMAKE_MATCH_1})
string(REGEX MATCH "USB_DID=([^\n]+)" _ ${USBID_OUTPUT})
set(USB_DID ${CMAKE_MATCH_1})

if(DEFINED USB_VID)
    set(CIRCLE_OPTIONS "${CIRCLE_OPTIONS} -o USB_GADGET_VENDOR_ID=${USB_VID}")
endif()
if(DEFINED USB_DID)
    set(CIRCLE_OPTIONS "${CIRCLE_OPTIONS} -o USB_GADGET_DEVICE_ID_BASE=${USB_DID}")
endif()

# Build circle-stdlib library
add_custom_target(circle-stdlib ALL
    # COMMAND make mrproper || true
    COMMAND ./configure -r $ENV{RPI} --prefix ${TOOLCHAIN_PREFIX} ${CIRCLE_OPTIONS} -o KERNEL_MAX_SIZE=0x400000
    COMMAND make -j
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/circle-stdlib
)

# Build additional libraries
add_custom_target(display ALL
    COMMAND make clean || true
    COMMAND make -j
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/circle-stdlib/libs/circle/addon/display
    DEPENDS circle-stdlib
)

add_custom_target(sensor ALL
    COMMAND make clean || true
    COMMAND make -j
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/circle-stdlib/libs/circle/addon/sensor
    DEPENDS circle-stdlib
)

add_custom_target(properties ALL
    COMMAND make clean || true
    COMMAND make -j
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/circle-stdlib/libs/circle/addon/Properties
    DEPENDS circle-stdlib
)
