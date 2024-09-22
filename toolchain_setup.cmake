# Set toolchain prefix and URLs based on RPI version
if($ENV{RPI} GREATER 2)
    message(STATUS "Using toolchain aarch64-none-elf")
    set(TOOLCHAIN_PREFIX "aarch64-none-elf-")
    set(TOOLCHAIN_URL "https://developer.arm.com/-/media/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-aarch64-none-elf.tar.xz")
else()
    message(STATUS "Using toolchain arm-none-eabi")
    set(TOOLCHAIN_PREFIX "arm-none-eabi-")
    set(TOOLCHAIN_URL "https://developer.arm.com/-/media/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-arm-none-eabi.tar.xz")
endif()

# Download and extract the toolchain if the toolchain is not yet installed
get_filename_component(TOOLCHAIN_FILENAME ${TOOLCHAIN_URL} NAME)
if(NOT EXISTS ${TOOLCHAIN_FILENAME})
    message(DEBUG "Downloading ${TOOLCHAIN_URL}...")
    execute_process(
        COMMAND wget -q ${TOOLCHAIN_URL}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

string(REGEX REPLACE "\\.tar\\.xz$" "" TOOLCHAIN_FILENAME_NO_EXT ${TOOLCHAIN_FILENAME})
string(REGEX MATCH "gcc-arm-[^/]*" TOOLCHAIN_DIR ${TOOLCHAIN_FILENAME_NO_EXT})
if(NOT EXISTS ${TOOLCHAIN_DIR})
    message(DEBUG "Extracting ${TOOLCHAIN_URL}...")
    execute_process(
        COMMAND tar xf ${TOOLCHAIN_DIR}.tar.xz
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

# Setup the toolchain that is required
execute_process(
    COMMAND readlink -f ./${TOOLCHAIN_DIR}/bin/
    OUTPUT_VARIABLE TOOLCHAIN_BIN_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

string(FIND "$ENV{PATH}" ${TOOLCHAIN_BIN_PATH} PATH_FOUND)
if(PATH_FOUND EQUAL -1)
    set(ENV{PATH} "${TOOLCHAIN_BIN_PATH}:$ENV{PATH}")
endif()
