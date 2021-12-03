cmake_minimum_required(VERSION 3.20.1)

project(r5bsp
    VERSION "1.0"
    DESCRIPTION "Apex Legends BSP viewer"
    LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there.")
endif()

add_subdirectory(src)