cmake_minimum_required(VERSION 3.20)
project(OpenUT99Plus CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wconversion)
endif()

add_library(ut STATIC engine/upkg.cpp engine/config.cpp engine/vfs.cpp)
target_include_directories(ut PUBLIC engine)

add_executable(ut_dump engine/ut_dump.cpp)
target_link_libraries(ut_dump PRIVATE ut)

enable_testing()
add_executable(test_engine engine/test_engine.cpp)
target_link_libraries(test_engine PRIVATE ut)
target_compile_options(test_engine PRIVATE -UNDEBUG)  # asserts must run in every build type
add_test(NAME engine COMMAND test_engine)
