cmake_minimum_required(VERSION 3.20)
project(famicom_rf_decoder CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math")

find_package(PkgConfig REQUIRED)
pkg_check_modules(HACKRF REQUIRED IMPORTED_TARGET libhackrf)
find_package(SDL2 REQUIRED CONFIG)

add_library(fam_dsp STATIC
    src/dsp/ntsc_decoder.cpp
)
target_include_directories(fam_dsp PUBLIC src)

add_executable(famidec
    src/main.cpp
    src/source/hackrf_source.cpp
    src/ui/sdl_display.cpp
)
target_link_libraries(famidec PRIVATE fam_dsp PkgConfig::HACKRF SDL2::SDL2)

enable_testing()
add_executable(synth_ntsc tests/synth_ntsc.cpp)
target_link_libraries(synth_ntsc PRIVATE fam_dsp)
add_test(NAME synth_ntsc COMMAND synth_ntsc)
