cmake_minimum_required(VERSION 3.18)
project(GalaxyDownmixerEffect LANGUAGES CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "The effect library is Android-only; build it with the NDK toolchain")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(GDM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(../dsp dsp)

add_library(galaxydownmixer SHARED src/EffectMain.cpp)
target_link_libraries(galaxydownmixer PRIVATE gdm_dsp log)
target_compile_options(galaxydownmixer PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
target_compile_options(gdm_dsp PRIVATE -fno-exceptions -fno-rtti)
# Only AELI is exported; keep the statically linked libc++ private so it can
# never clash with other effect libraries in the HAL process.
target_link_options(galaxydownmixer PRIVATE
    -Wl,--exclude-libs,ALL
    -Wl,--no-undefined
    -Wl,-z,noexecstack
    -Wl,--build-id=sha1)
set_target_properties(galaxydownmixer PROPERTIES OUTPUT_NAME galaxydownmixer)

# On-device smoke test for the library (plain executable, run via adb shell).
add_executable(effect_host_test tools/effect_host_test.cpp)
target_link_libraries(effect_host_test PRIVATE dl)
