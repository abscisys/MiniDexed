cmake_minimum_required(VERSION 3.28)
project(MiniDexed)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Check if RPI is set
if(NOT DEFINED ENV{RPI})
    message(WARNING "RPI environment variable is not defined, defaulting it to 4: RPI=4")
    set(ENV{RPI} "4")
endif()

if(DEFINED ENV{DEBUG_BUILD})
    message(WARNING "Running build in debug mode")
    set(CMAKE_VERBOSE_MAKEFILE ON)
    execute_process(
        COMMAND set -e && set -x
    )
endif()

# Include the toolchain setup
include(${CMAKE_SOURCE_DIR}/toolchain_setup.cmake)

# Include the build of the ciurcle libraries and addons
include(${CMAKE_SOURCE_DIR}/circle_dependencies_build.cmake)

# Build MiniDexed
add_custom_target(MiniDexed ALL
    COMMAND make clean || true
    COMMAND make -j
    COMMAND ls *.img
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src
    DEPENDS display sensor properties
)
