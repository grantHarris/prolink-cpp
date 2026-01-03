cmake_minimum_required(VERSION 3.16)

project(prolink_cpp LANGUAGES CXX)

add_library(prolink_cpp
  src/prolink.cpp
)

target_include_directories(prolink_cpp PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(prolink_cpp PUBLIC cxx_std_17)

option(PROLINK_BUILD_TESTS "Build prolink_cpp tests" ON)
option(PROLINK_FETCH_GTEST "Fetch GoogleTest if not found" OFF)

add_executable(prolink_listener
  examples/listener.cpp
)
target_link_libraries(prolink_listener PRIVATE prolink_cpp)

add_executable(prolink_virtual_cdj
  examples/virtual_cdj.cpp
)
target_link_libraries(prolink_virtual_cdj PRIVATE prolink_cpp)

add_executable(prolink_control_demo
  examples/control_demo.cpp
)
target_link_libraries(prolink_control_demo PRIVATE prolink_cpp)

add_executable(prolink_virtual_cdj_interactive
  examples/virtual_cdj_interactive.cpp
)
target_link_libraries(prolink_virtual_cdj_interactive PRIVATE prolink_cpp)

if(PROLINK_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND AND PROLINK_FETCH_GTEST)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  if(GTest_FOUND OR TARGET gtest)
    add_executable(prolink_tests
      tests/test_basic.cpp
      tests/test_packets.cpp
      tests/test_packet_parsing.cpp
      tests/test_beat_clock.cpp
      tests/test_config.cpp
      tests/test_device_tracking.cpp
      tests/test_thread_safety.cpp
    )
    target_compile_definitions(prolink_cpp PRIVATE PROLINK_TESTING)
    target_compile_definitions(prolink_tests PRIVATE PROLINK_TESTING)
    if(TARGET GTest::gtest_main)
      target_link_libraries(prolink_tests PRIVATE prolink_cpp GTest::gtest_main)
    else()
      target_link_libraries(prolink_tests PRIVATE prolink_cpp gtest_main)
    endif()
    add_test(NAME prolink_tests COMMAND prolink_tests)
  else()
    message(WARNING "GTest not found; tests are disabled. Install GTest or set PROLINK_FETCH_GTEST=ON.")
  endif()
endif()
