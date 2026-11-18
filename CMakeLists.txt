cmake_minimum_required(VERSION 3.22)
project(OpenDrums VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(OPENDRUMS_BUILD_PLUGIN "Build the VST3 plugin (needs JUCE, fetched from GitHub)" ON)
option(OPENDRUMS_BUILD_TOOLS  "Build the core tests and the offline renderer" ON)

# ---------------------------------------------------------------- core (no JUCE)
add_library(opendrums_core STATIC
    core/Sample.cpp
    core/Kit.cpp
    core/Library.cpp
    core/Rig.cpp
    core/KitGeometry.cpp
    core/Eq.cpp
    core/Compressor.cpp
    core/Reverb.cpp
    core/Mixer.cpp
    core/SceneGeometry.cpp
    core/Smf.cpp
    core/Grooves.cpp
    core/Engine.cpp)
target_include_directories(opendrums_core PUBLIC core third_party)
if(MSVC)
    target_compile_options(opendrums_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(opendrums_core PRIVATE -Wall -Wextra)
endif()

if(OPENDRUMS_BUILD_TOOLS)
    enable_testing()
    add_executable(core_test tests/core_test.cpp)
    target_link_libraries(core_test PRIVATE opendrums_core)
    add_executable(render_demo tools/render_demo.cpp)
    target_link_libraries(render_demo PRIVATE opendrums_core)
    add_executable(preview_kit tools/preview_kit.cpp)
    target_link_libraries(preview_kit PRIVATE opendrums_core)
    add_test(NAME core COMMAND core_test ${CMAKE_SOURCE_DIR}/kits)
endif()

# ---------------------------------------------------------------- plugin (JUCE)
if(OPENDRUMS_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
