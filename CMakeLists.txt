cmake_minimum_required(VERSION 3.20)

project(hdrcal VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
    message(FATAL_ERROR "hdrcal exige GCC >= 11 (C++20: designated initializers, ranges e charconv); encontrado ${CMAKE_CXX_COMPILER_VERSION}")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR "hdrcal exige Clang >= 14 (C++20: designated initializers, ranges e charconv); encontrado ${CMAKE_CXX_COMPILER_VERSION}")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm>=2.4.99)
find_package(ZLIB REQUIRED)

add_library(hdrcal_core STATIC
    src/atomic_file.cpp
    src/command.cpp
    src/hypr.cpp
    src/hypr_config.cpp
    src/integrate.cpp
    src/json.cpp
    src/key_source.cpp
    src/kms.cpp
    src/mpv.cpp
    src/pattern.cpp
    src/pq.cpp
    src/preflight.cpp
    src/process.cpp
    src/profile.cpp
    src/watchdog.cpp
    src/wizard.cpp
    src/wizard_session.cpp
)
target_include_directories(hdrcal_core PUBLIC include)
target_link_libraries(hdrcal_core PUBLIC PkgConfig::LIBDRM ZLIB::ZLIB)

add_executable(hdrcal src/main.cpp)
target_link_libraries(hdrcal PRIVATE hdrcal_core)

install(TARGETS hdrcal
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

enable_testing()

add_executable(hdrcal_tests
    tests/test_command.cpp
    tests/test_hypr.cpp
    tests/test_hypr_config.cpp
    tests/test_integrate.cpp
    tests/test_json.cpp
    tests/test_key_source.cpp
    tests/test_kms.cpp
    tests/test_main.cpp
    tests/test_mpv.cpp
    tests/test_number_format.cpp
    tests/test_pattern.cpp
    tests/test_pq.cpp
    tests/test_process.cpp
    tests/test_preflight.cpp
    tests/test_profile.cpp
    tests/test_watchdog.cpp
    tests/test_wizard.cpp
)
target_link_libraries(hdrcal_tests PRIVATE hdrcal_core)

add_test(NAME hdrcal_unit_tests COMMAND hdrcal_tests)

add_test(NAME hdrcal_install_smoke
    COMMAND ${CMAKE_COMMAND}
        -DHDRCAL_BUILD_DIR=${CMAKE_BINARY_DIR}
        -DHDRCAL_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/install-smoke
        -DHDRCAL_INSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_smoke.cmake
)
