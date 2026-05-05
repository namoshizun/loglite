cmake_minimum_required(VERSION 3.25)
project(loglite VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp"
    @ONLY
)

# Conan CMakeDeps lives in the build dir.
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}")

# ── Dependencies ──────────────────────────────────────────────────────────────

find_package(Boost REQUIRED CONFIG)
find_package(SQLite3 REQUIRED CONFIG)
find_package(yaml-cpp REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(CLI11 REQUIRED CONFIG)

option(LOGLITE_PYTHON "Build the pybind11 extension module (_core)" OFF)
if(LOGLITE_PYTHON)
    find_package(pybind11 REQUIRED CONFIG)
endif()

include(CTest)

# ── Main library target ───────────────────────────────────────────────────────

file(GLOB LOGLITE_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(REMOVE_ITEM LOGLITE_LIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bindings.cpp"
)

add_library(loglite_lib STATIC ${LOGLITE_LIB_SOURCES})

target_include_directories(loglite_lib PUBLIC
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(loglite_lib PUBLIC
    boost::boost
    SQLite::SQLite3
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
    CLI11::CLI11
)
target_compile_options(loglite_lib PUBLIC -Wall -Wextra -Wno-unused-parameter)

# Asio requires this on macOS to use epoll/kqueue
target_compile_definitions(loglite_lib PUBLIC
    BOOST_ASIO_HAS_CO_AWAIT
    BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT
)

# ── Executable ────────────────────────────────────────────────────────────────

add_executable(loglite src/main.cpp)
target_link_libraries(loglite PRIVATE loglite_lib)

# ── Release hardening ─────────────────────────────────────────────────────────
#
# Enabled automatically when build.sh --release is used.
# Can also be set manually: cmake -DLOGLITE_LTO=ON -DLOGLITE_STRIP=ON ...

option(LOGLITE_LTO   "Enable link-time optimisation"          OFF)
option(LOGLITE_STRIP "Strip debug symbols from the binary"    OFF)
option(LOGLITE_STATIC_MUSL "Build a fully static Linux/musl executable" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Hide all non-exported symbols: shrinks the binary and prevents internal
    # function / variable names from appearing in nm / strings output.
    target_compile_options(loglite_lib PUBLIC
        -fvisibility=hidden
        -fvisibility-inlines-hidden
    )
    # Eliminate unreachable code and data at link time (macOS linker flag).
    if(APPLE)
        target_link_options(loglite PRIVATE -Wl,-dead_strip)
    else()
        target_compile_options(loglite_lib PUBLIC -ffunction-sections -fdata-sections)
        target_link_options(loglite PRIVATE -Wl,--gc-sections)
    endif()
endif()

if(LOGLITE_STATIC_MUSL)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "LOGLITE_STATIC_MUSL is only supported on Linux")
    endif()
    target_link_options(loglite PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(LOGLITE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
    if(_ipo_ok)
        set_property(TARGET loglite     PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_property(TARGET loglite_lib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO enabled")
    else()
        message(WARNING "LTO requested but not supported: ${_ipo_msg}")
    endif()
endif()

if(LOGLITE_STRIP)
    if(APPLE)
        # -x  remove local (non-exported) symbols
        # -S  remove debug symbol table (STABS / DWARF)
        add_custom_command(TARGET loglite POST_BUILD
            COMMAND strip -x -S $<TARGET_FILE:loglite>
            COMMENT "Stripping debug symbols from loglite")
    else()
        add_custom_command(TARGET loglite POST_BUILD
            COMMAND strip --strip-all $<TARGET_FILE:loglite>
            COMMENT "Stripping loglite")
    endif()
endif()

# ── Coverage instrumentation ──────────────────────────────────────────────────
#
# Applies gcov instrumentation to loglite_lib so run-tests.sh --cov can produce
# a complete lcov report.  Owned here because loglite_lib is defined here.
# build.sh always passes -DLOGLITE_COVERAGE=OFF to guarantee a clean binary.

option(LOGLITE_COVERAGE "Instrument loglite_lib with gcov (--coverage)" OFF)
if(LOGLITE_COVERAGE)
    target_compile_options(loglite_lib PRIVATE --coverage)
    target_link_options(loglite        PRIVATE --coverage)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(loglite_lib PRIVATE -fprofile-update=atomic)
    endif()
endif()

# ── Python extension ──────────────────────────────────────────────────────────

if(LOGLITE_PYTHON)
    # Required when this archive is linked into a shared module (pybind11 _core.so).
    set_property(TARGET loglite_lib PROPERTY POSITION_INDEPENDENT_CODE ON)
    pybind11_add_module(_core src/bindings.cpp)
    target_link_libraries(_core PRIVATE loglite_lib)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
# Controlled by BUILD_TESTING (build.sh passes OFF; run-tests.sh passes ON).
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
