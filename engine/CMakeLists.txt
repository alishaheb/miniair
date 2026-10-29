cmake_minimum_required(VERSION 3.20)
project(miniair VERSION 0.1.0 LANGUAGES CXX)

# MISRA C++:2023 is written against C++17, so the engine targets C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MINIAIR_WITH_KAFKA "Build the Kafka input/output (needs librdkafka++)" ON)
option(MINIAIR_BUILD_TESTS "Build the GoogleTest unit tests" ON)
option(MINIAIR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

# ---- Dependencies: use the system package if present, otherwise download ----
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found, fetching v3.11.3")
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(json)
endif()

# Strict warnings for project code only (not third-party code).
set(MINIAIR_STRICT_WARNINGS
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
  -Wold-style-cast -Wcast-qual -Wnull-dereference -Wdouble-promotion
  -Wfloat-equal -Wimplicit-fallthrough -Wnon-virtual-dtor -Woverloaded-virtual)
if(MINIAIR_WARNINGS_AS_ERRORS)
  list(APPEND MINIAIR_STRICT_WARNINGS -Werror)
endif()

# ---- Core library: pure logic, no I/O, no Kafka ----
add_library(miniair_core
  src/alert_manager.cpp
  src/conflict_detector.cpp
  src/engine.cpp
  src/geo.cpp
  src/message_codec.cpp
  src/track_store.cpp)
target_include_directories(miniair_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(miniair_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(miniair_core PRIVATE ${MINIAIR_STRICT_WARNINGS})

# ---- Executable ----
add_executable(miniair-engine src/main.cpp)
target_link_libraries(miniair-engine PRIVATE miniair_core)
target_compile_options(miniair-engine PRIVATE ${MINIAIR_STRICT_WARNINGS})

if(MINIAIR_WITH_KAFKA)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(RDKAFKA REQUIRED IMPORTED_TARGET rdkafka++)
  target_sources(miniair-engine PRIVATE src/kafka_io.cpp)
  target_link_libraries(miniair-engine PRIVATE PkgConfig::RDKAFKA)
  target_compile_definitions(miniair-engine PRIVATE MINIAIR_WITH_KAFKA)
endif()

install(TARGETS miniair-engine RUNTIME DESTINATION bin)

# ---- Tests ----
if(MINIAIR_BUILD_TESTS)
  enable_testing()
  find_package(GTest 1.11 QUIET)
  if(NOT GTest_FOUND)
    message(STATUS "GoogleTest not found, fetching v1.15.2")
    FetchContent_Declare(googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(miniair_tests
    tests/test_alert_manager.cpp
    tests/test_conflict_detector.cpp
    tests/test_engine.cpp
    tests/test_geo.cpp
    tests/test_message_codec.cpp
    tests/test_track_store.cpp)
  target_link_libraries(miniair_tests PRIVATE miniair_core nlohmann_json::nlohmann_json GTest::gtest_main)
  target_compile_options(miniair_tests PRIVATE -Wall -Wextra -Wpedantic)

  include(GoogleTest)
  gtest_discover_tests(miniair_tests)

  # End-to-end check of the stdin/stdout executable on a recorded scenario.
  add_test(NAME cli_synthetic_scenario
    COMMAND ${CMAKE_COMMAND}
      -DENGINE=$<TARGET_FILE:miniair-engine>
      -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/synthetic_scenario.jsonl
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake)
endif()
