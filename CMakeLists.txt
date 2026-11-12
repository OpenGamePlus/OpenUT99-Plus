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

add_library(ut STATIC engine/upkg.cpp engine/config.cpp engine/vfs.cpp engine/texture.cpp engine/level.cpp engine/packages.cpp engine/png.cpp engine/scene.cpp engine/lighting.cpp engine/script.cpp engine/mesh.cpp engine/vm.cpp engine/world.cpp engine/world_ai.cpp engine/world_mover.cpp engine/collision.cpp engine/play_view.cpp)
target_include_directories(ut PUBLIC engine)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

add_library(ut_render STATIC engine/render.cpp)
target_link_libraries(ut_render PUBLIC ut PRIVATE PkgConfig::EPOXY)

add_executable(ut_mapview engine/mapview.cpp)
target_link_libraries(ut_mapview PRIVATE ut_render PkgConfig::SDL2)

add_executable(ut_dump engine/ut_dump.cpp)
target_link_libraries(ut_dump PRIVATE ut)

add_executable(ut_script engine/ut_script.cpp)
target_link_libraries(ut_script PRIVATE ut)

enable_testing()
add_executable(test_engine engine/test_engine.cpp)
target_link_libraries(test_engine PRIVATE ut)
target_compile_options(test_engine PRIVATE -UNDEBUG)  # asserts must run in every build type
add_test(NAME engine COMMAND test_engine)
