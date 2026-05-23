cmake_minimum_required(VERSION 3.25)
project(burn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}")

find_package(Boost REQUIRED CONFIG)
find_package(CLI11 REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(fmt REQUIRED CONFIG)

add_executable(burn src/main.cpp)

target_include_directories(burn PRIVATE src)
target_link_libraries(burn PRIVATE boost::boost CLI11::CLI11 nlohmann_json::nlohmann_json fmt::fmt)

target_compile_definitions(burn PRIVATE
    BOOST_ASIO_HAS_CO_AWAIT
    BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT
)

target_compile_options(burn PRIVATE -Wall -Wextra -Wno-unused-parameter)

option(BURN_LTO "Enable link-time optimisation" OFF)
option(BURN_STRIP "Strip debug symbols from the binary" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(burn PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -march=native
    )
    if(APPLE)
        target_link_options(burn PRIVATE -Wl,-dead_strip)
    else()
        target_compile_options(burn PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(burn PRIVATE -Wl,--gc-sections)
    endif()
endif()

if(BURN_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
    if(_ipo_ok)
        set_property(TARGET burn PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO enabled")
    else()
        message(WARNING "LTO requested but not supported: ${_ipo_msg}")
    endif()
endif()

if(BURN_STRIP)
    if(APPLE)
        add_custom_command(TARGET burn POST_BUILD
            COMMAND strip -x -S $<TARGET_FILE:burn>
            COMMENT "Stripping debug symbols from burn")
    else()
        add_custom_command(TARGET burn POST_BUILD
            COMMAND strip --strip-all $<TARGET_FILE:burn>
            COMMENT "Stripping burn")
    endif()
endif()
