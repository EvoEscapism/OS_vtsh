cmake_minimum_required(VERSION 3.16)
project(vtsh C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "VTSH uses the Linux clone(2) API. Build it inside Linux or WSL Ubuntu.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_compile_definitions(_GNU_SOURCE)

add_executable(vtsh
    src/main.c
    src/parser.c
    src/process.c
    src/builtins.c
)

target_include_directories(vtsh PRIVATE include)
target_compile_options(vtsh PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(vtsh PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nofile_probe helpers/nofile_probe.c)
target_compile_options(nofile_probe PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(nofile_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(signal_probe helpers/signal_probe.c)
target_compile_options(signal_probe PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(signal_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME vtsh_python_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_vtsh.py
    )
    set_tests_properties(vtsh_python_tests PROPERTIES
        ENVIRONMENT "VTSH_BIN=$<TARGET_FILE:vtsh>;NOFILE_PROBE_BIN=$<TARGET_FILE:nofile_probe>;SIGNAL_PROBE_BIN=$<TARGET_FILE:signal_probe>"
    )
else()
    message(STATUS "Python 3 not found: CTest integration tests are disabled.")
endif()
