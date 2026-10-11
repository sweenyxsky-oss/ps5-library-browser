cmake_minimum_required(VERSION 3.16)
project(ps5-library-browser VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

# Core library
add_library(ps5_library_core STATIC
    src/daemon_client.cpp
)

target_include_directories(ps5_library_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

find_package(Threads REQUIRED)
target_link_libraries(ps5_library_core PUBLIC
    Threads::Threads
)

# Testing
enable_testing()

add_executable(daemon_client_test
    tests/daemon_client_test.cpp
)

target_link_libraries(daemon_client_test PRIVATE
    ps5_library_core
    Threads::Threads
)

add_test(NAME daemon_client_test COMMAND daemon_client_test)
