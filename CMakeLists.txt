cmake_minimum_required(VERSION 3.10)
project(simulator)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# export the core files as a library (also imported by Swift wrapper library)
add_library(simulator_lib STATIC
  src/processor.cpp
  src/assembler.cpp
)
target_include_directories(simulator_lib
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

add_executable(simulator
  src/main.cpp
)
target_include_directories(simulator
  PRIVATE ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(simulator
  simulator_lib
)

# google test configuration
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(simulator_test
  tests/memory_test.cpp
  tests/processor_test.cpp
  tests/assembler_test.cpp
)
target_include_directories(simulator_test
  PRIVATE ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(simulator_test
  simulator_lib
  GTest::gtest_main
  GTest::gmock_main   # bring in Google Mock
)

include(GoogleTest)
gtest_discover_tests(simulator_test)
