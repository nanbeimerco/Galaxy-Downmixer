cmake_minimum_required(VERSION 3.18)
project(GalaxyDownmixerDsp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GDM_BUILD_TESTS "Build the PC unit tests and tools" ON)

add_library(gdm_dsp STATIC
    src/ApoParser.cpp
    src/Biquad.cpp
    src/ChannelMap.cpp
    src/Engine.cpp
    src/Options.cpp
)
target_include_directories(gdm_dsp PUBLIC include)

if(MSVC)
    target_compile_options(gdm_dsp PRIVATE /W4 /permissive- /fp:precise)
    target_compile_definitions(gdm_dsp PUBLIC _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(gdm_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ANDROID)
    # Effect libraries are loaded by the audio HAL; keep the code position
    # independent so it can be linked into a shared object.
    set_target_properties(gdm_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(GDM_BUILD_TESTS AND NOT ANDROID)
    enable_testing()

    add_executable(gdm_tests tests/test_main.cpp)
    target_link_libraries(gdm_tests PRIVATE gdm_dsp)
    target_compile_definitions(gdm_tests PRIVATE
        GDM_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME gdm_tests COMMAND gdm_tests)

    add_executable(gdm_cli tools/gdm_cli.cpp)
    target_link_libraries(gdm_cli PRIVATE gdm_dsp)
endif()
