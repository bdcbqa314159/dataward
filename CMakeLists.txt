cmake_minimum_required(VERSION 3.21)
project(dataward VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clangd / editor language server

add_library(dataward src/version.cpp)
add_library(dataward::dataward ALIAS dataward)
target_compile_features(dataward PUBLIC cxx_std_20)
target_include_directories(dataward PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_definitions(dataward PRIVATE DATAWARD_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(dataward PRIVATE /W4)
else()
  target_compile_options(dataward PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests: ON when dataward is the top-level project, OFF when pulled in via
# FetchContent by a consumer.
option(DATAWARD_BUILD_TESTS "Build dataward's tests" ${PROJECT_IS_TOP_LEVEL})
if(DATAWARD_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2 GIT_SHALLOW TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # MSVC
  FetchContent_MakeAvailable(googletest)

  add_executable(dataward_tests tests/version_test.cpp)
  target_link_libraries(dataward_tests PRIVATE dataward::dataward GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(dataward_tests)
endif()
