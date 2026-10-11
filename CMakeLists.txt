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
    src/startup_diagnostics.cpp
)

target_include_directories(ps5_library_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

find_package(Threads REQUIRED)
target_link_libraries(ps5_library_core PUBLIC
    Threads::Threads
)

# Dedicated PS5 Application & Payload Target
add_executable(ps5_library_browser_elf src/main.cpp)
set_target_properties(ps5_library_browser_elf PROPERTIES 
    OUTPUT_NAME "ps5-library-browser.elf"
)
target_link_libraries(ps5_library_browser_elf PRIVATE 
    ps5_library_core 
    Threads::Threads
)

# PS5 SDK stubs when cross-compiling via prospero.cmake / pacbrew
if(PROSPERO OR PS5)
    target_compile_definitions(ps5_library_browser_elf PRIVATE __PROSPERO__=1 __PS5__=1)
    target_link_libraries(ps5_library_browser_elf PRIVATE
        -lkernel
        -lSceSystemService
        -lSceNotification
        -lSceAppInstUtil
        -lSceNet
        -lSceUserService
    )
endif()

# Testing
enable_testing()

# Milestone 1 Tests
add_executable(daemon_client_test tests/daemon_client_test.cpp)
target_link_libraries(daemon_client_test PRIVATE ps5_library_core Threads::Threads)
add_test(NAME daemon_client_test COMMAND daemon_client_test)

# Milestone 2 Tests
add_executable(scanner_cache_test tests/scanner_cache_test.cpp)
target_link_libraries(scanner_cache_test PRIVATE ps5_library_core Threads::Threads)
add_test(NAME scanner_cache_test COMMAND scanner_cache_test)

# Milestone 3 Tests
add_executable(ui_shell_test tests/ui_shell_test.cpp)
target_link_libraries(ui_shell_test PRIVATE ps5_library_core Threads::Threads)
add_test(NAME ui_shell_test COMMAND ui_shell_test)

# Milestone 4 Tests
add_executable(inspector_installer_test tests/inspector_installer_test.cpp)
target_link_libraries(inspector_installer_test PRIVATE ps5_library_core Threads::Threads)
add_test(NAME inspector_installer_test COMMAND inspector_installer_test)

# Milestone 4 Extensions Tests
add_executable(ps5_installer_persistence_test tests/ps5_installer_persistence_test.cpp)
target_link_libraries(ps5_installer_persistence_test PRIVATE ps5_library_core Threads::Threads)
add_test(NAME ps5_installer_persistence_test COMMAND ps5_installer_persistence_test)

add_executable(install_retry_preflight_history_test tests/install_retry_preflight_history_test.cpp)
target_link_libraries(install_retry_preflight_history_test PRIVATE ps5_library_core Threads::Threads)
add_test(NAME install_retry_preflight_history_test COMMAND install_retry_preflight_history_test)

# Startup Diagnostics Tests
add_executable(startup_diagnostics_test tests/startup_diagnostics_test.cpp)
target_link_libraries(startup_diagnostics_test PRIVATE ps5_library_core Threads::Threads)
add_test(NAME startup_diagnostics_test COMMAND startup_diagnostics_test)
