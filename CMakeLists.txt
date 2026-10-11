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
    src/game_metadata.cpp
    src/scanner.cpp
    src/index_cache.cpp
    src/input.cpp
    src/views.cpp
    src/ui_shell.cpp
    src/installer.cpp
    src/inspector.cpp
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

# Milestone 1 Tests
add_executable(daemon_client_test
    tests/daemon_client_test.cpp
)
target_link_libraries(daemon_client_test PRIVATE
    ps5_library_core
    Threads::Threads
)
add_test(NAME daemon_client_test COMMAND daemon_client_test)

# Milestone 2 Tests
add_executable(scanner_cache_test
    tests/scanner_cache_test.cpp
)
target_link_libraries(scanner_cache_test PRIVATE
    ps5_library_core
    Threads::Threads
)
add_test(NAME scanner_cache_test COMMAND scanner_cache_test)

# Milestone 3 native UI state/layout/input tests
add_executable(ui_shell_test tests/ui_shell_test.cpp)
target_link_libraries(ui_shell_test PRIVATE ps5_library_core Threads::Threads)
add_test(NAME ui_shell_test COMMAND ui_shell_test)

# Milestone 4 package inspector and install pipeline tests
add_executable(inspector_installer_test tests/inspector_installer_test.cpp)
target_link_libraries(inspector_installer_test PRIVATE ps5_library_core Threads::Threads)
add_test(NAME inspector_installer_test COMMAND inspector_installer_test)
