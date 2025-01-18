cmake_minimum_required(VERSION 3.28)

if(NOT EXISTS "${CMAKE_BINARY_DIR}/sounds/Soundplantage")
    execute_process(
        COMMAND git clone --depth 1 https://github.com/Banana71/Soundplantage "${CMAKE_BINARY_DIR}/sounds/Soundplantage"
        RESULT_VARIABLE GIT_RESULT
        ERROR_VARIABLE GIT_ERROR
    )
    
    if(NOT GIT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone Soundplantage repository: ${GIT_ERROR}")
    endif()
else()
    message(STATUS "Soundplantage repository already exists")
endif()