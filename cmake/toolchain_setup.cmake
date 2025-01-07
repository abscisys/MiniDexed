cmake_minimum_required(VERSION 3.28)

set(TOOLCHAIN_HOME ${CMAKE_BINARY_DIR}/../toolchain CACHE FILEPATH "Toolchain home directory")

# Set toolchain prefix and URLs based on RPI version
if(RPI GREATER 2)
    set(TOOLCHAIN_NAME "aarch64-none-elf" CACHE STRING "Toolchain name")
    message(STATUS "Using toolchain ${TOOLCHAIN_NAME}")
    set(TOOLCHAIN_PREFIX "${TOOLCHAIN_NAME}-" CACHE STRING "Toolchain prefix")
    set(TOOLCHAIN_URL "https://developer.arm.com/-/media/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-aarch64-none-elf.tar.xz" CACHE STRING "Toolchain URL")
else()
    set(TOOLCHAIN_NAME "arm-none-eabi" CACHE STRING "Toolchain name")
    message(STATUS "Using toolchain ${TOOLCHAIN_NAME}")
    set(TOOLCHAIN_PREFIX "${TOOLCHAIN_NAME}-" CACHE STRING "Toolchain prefix")
    set(TOOLCHAIN_URL "https://developer.arm.com/-/media/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-arm-none-eabi.tar.xz" CACHE STRING "Toolchain URL")
endif()

# Download and extract the toolchain if the toolchain is not yet installed
get_filename_component(TOOLCHAIN_FILENAME ${TOOLCHAIN_URL} NAME)
if(NOT EXISTS ${TOOLCHAIN_HOME}/${TOOLCHAIN_FILENAME})
    message(STATUS "Downloading ${TOOLCHAIN_URL}...")
    if(NOT EXISTS ${TOOLCHAIN_HOME})
        file(MAKE_DIRECTORY ${TOOLCHAIN_HOME})
    endif()
    execute_process(
        COMMAND wget -q ${TOOLCHAIN_URL}
        WORKING_DIRECTORY "${TOOLCHAIN_HOME}" 
        RESULT_VARIABLE wget_result 
    )

    # Check if wget command was successful 
    if(wget_result EQUAL 0) 
        message(STATUS "Download completed successfully.")
    else()
        message(FATAL_ERROR "Download failed: ${wget_result}")
    endif()
endif()

string(REGEX REPLACE "\\.tar\\.xz$" "" TOOLCHAIN_FILENAME_NO_EXT ${TOOLCHAIN_FILENAME})
string(REGEX MATCH "gcc-arm-[^/]*" TOOLCHAIN_DIR ${TOOLCHAIN_FILENAME_NO_EXT})
if(NOT EXISTS ${TOOLCHAIN_HOME}/${TOOLCHAIN_DIR})
    message(STATUS "Extracting ${TOOLCHAIN_URL}...")
    execute_process(
        COMMAND tar xf ${TOOLCHAIN_HOME}/${TOOLCHAIN_DIR}.tar.xz
        WORKING_DIRECTORY ${TOOLCHAIN_HOME}
        RESULT_VARIABLE tar_result 
    ) 
        
    # Check if tar command was successful 
    if(tar_result EQUAL 0) 
        message(STATUS "Extraction completed successfully.") 
    else()
        message(FATAL_ERROR "Extraction failed: ${tar_result}")
    endif()
endif()

# Setup the toolchain that is required
execute_process(
    COMMAND readlink -f ${TOOLCHAIN_HOME}/${TOOLCHAIN_DIR}/bin/
    RESULT_VARIABLE toolchain_readlink_result 
    OUTPUT_VARIABLE TOOLCHAIN_BIN_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Check if TOOLCHAIN_BIN_PATH is already in PATH
if(NOT "$ENV{PATH}" MATCHES "${TOOLCHAIN_BIN_PATH}")
    if(toolchain_readlink_result EQUAL 0)
        message(STATUS "Toolchain path: ${TOOLCHAIN_BIN_PATH}")
        set(TOOLCHAIN_BIN_PATH ${TOOLCHAIN_BIN_PATH} CACHE FILEPATH "Toolchain bin path" FORCE)
        set(ENV{PATH} "$ENV{PATH}:${TOOLCHAIN_BIN_PATH}")
    else()
        message(FATAL_ERROR "Toolchain path could not be determined")
    endif()
endif()

# Setup environment file
set(ENV_FILE "${CMAKE_BINARY_DIR}/env_vars.sh" CACHE FILEPATH "Environment file")
file(WRITE  "${ENV_FILE}" "#!/bin/sh\n")
file(APPEND "${ENV_FILE}" "export PATH=\"$ENV{PATH}\"\n")
file(APPEND "${ENV_FILE}" "export TOOLCHAIN_PREFIX=\"${TOOLCHAIN_PREFIX}\"\n")
execute_process(COMMAND chmod +x ${ENV_FILE})

# Specify the cross compiler
set(CMAKE_C_COMPILER ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}gcc CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}g++ CACHE FILEPATH "C++ compiler" FORCE)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}as CACHE FILEPATH "ASM compiler" FORCE)
set(CMAKE_LINKER ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}ld CACHE FILEPATH "Linker" FORCE)
set(CMAKE_C_LINK_EXECUTABLE "${CMAKE_LINKER} <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>" CACHE STRING "Instruction to use global linker for C code" FORCE)
set(CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_LINKER} <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>" CACHE STRING "Instruction to use global linker for C++ code" FORCE)
set(CMAKE_AR ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}ar CACHE FILEPATH "Archiver" FORCE)
set(CMAKE_C_COMPILER_AR ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}ar CACHE FILEPATH "Archiver" FORCE)
set(CMAKE_CXX_COMPILER_AR ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}ar CACHE FILEPATH "Archiver" FORCE)
set(CMAKE_C_CREATE_SHARED_LIBRARY "${CMAKE_LINKER}" CACHE FILEPATH "Linker for shared C library" FORCE)
set(CMAKE_CXX_CREATE_SHARED_LIBRARY "${CMAKE_LINKER}" CACHE FILEPATH "Linker for shared C++ library" FORCE)
set(CMAKE_C_CREATE_STATIC_LIBRARY "${CMAKE_AR}" CACHE FILEPATH "Linker for static C library" FORCE)
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "${CMAKE_AR}" CACHE FILEPATH "Linker for static C++ library" FORCE)
set(CMAKE_OBJCOPY ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}objcopy CACHE FILEPATH "Object copy" FORCE)
set(CMAKE_OBJDUMP ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}objdump CACHE FILEPATH "Object dump" FORCE)
set(CMAKE_CXXFILT ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}c++filt CACHE FILEPATH "C++ filter" FORCE)
set(CMAKE_SIZE ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}size CACHE FILEPATH "Size" FORCE)
set(CMAKE_STRIP ${TOOLCHAIN_BIN_PATH}/${TOOLCHAIN_PREFIX}strip CACHE FILEPATH "Strip" FORCE)

# Set the output extensions for object files
set(CMAKE_C_COMPILER_SUFFIX_VAR "o" CACHE STRING "Object file extension for the C source code" FORCE)
set(CMAKE_CXX_COMPILER_SUFFIX_VAR "o" CACHE STRING "Object file extension for the C++ source code" FORCE)
set_property(GLOBAL PROPERTY SUFFIX_VAR "o")

# System information
set(CMAKE_SYSTEM_NAME Generic CACHE STRING "Target system name")
set(CMAKE_SYSTEM_PROCESSOR arm CACHE STRING "Target processor")
set(CMAKE_CROSSCOMPILING 1 CACHE BOOL "Cross-compiling flag")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY CACHE STRING "Try compile target type")

# Search settings
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER CACHE STRING "Find program mode")
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY CACHE STRING "Find library mode")
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY CACHE STRING "Find include mode")
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY CACHE STRING "Find package mode")

unset(TOOLCHAIN_URL)
unset(TOOLCHAIN_FILENAME)
unset(TOOLCHAIN_FILENAME_NO_EXT)
unset(TOOLCHAIN_DIR)
unset(TOOLCHAIN_BIN_PATH)
unset(wget_result)
unset(tar_result)
unset(toolchain_readlink_result)
