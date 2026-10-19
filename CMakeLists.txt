cmake_minimum_required(VERSION 3.20)

project(hdrcal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)

add_library(hdrcal_core STATIC
    src/command.cpp
    src/hypr.cpp
    src/json.cpp
    src/kms.cpp
    src/pq.cpp
    src/preflight.cpp
    src/profile.cpp
)
target_include_directories(hdrcal_core PUBLIC include)
target_link_libraries(hdrcal_core PUBLIC PkgConfig::LIBDRM)

add_executable(hdrcal src/main.cpp)
target_link_libraries(hdrcal PRIVATE hdrcal_core)

enable_testing()

add_executable(hdrcal_tests
    tests/test_command.cpp
    tests/test_hypr.cpp
    tests/test_json.cpp
    tests/test_kms.cpp
    tests/test_main.cpp
    tests/test_pq.cpp
    tests/test_preflight.cpp
    tests/test_profile.cpp
)
target_link_libraries(hdrcal_tests PRIVATE hdrcal_core)

add_test(NAME hdrcal_unit_tests COMMAND hdrcal_tests)
