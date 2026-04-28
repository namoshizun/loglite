cmake_minimum_required(VERSION 3.25)
project(loglite CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Homebrew prefix for macOS
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew)
endif()

# ── Dependencies ──────────────────────────────────────────────────────────────

find_package(Boost 1.80 REQUIRED)
find_package(SQLite3 REQUIRED)

include(FetchContent)

FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    SYSTEM
    OVERRIDE_FIND_PACKAGE
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    SYSTEM
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.6.2
    SYSTEM
)

FetchContent_MakeAvailable(yaml-cpp nlohmann_json CLI11)

# ── Main library target ───────────────────────────────────────────────────────

file(GLOB LOGLITE_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(REMOVE_ITEM LOGLITE_LIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)

add_library(loglite_lib STATIC ${LOGLITE_LIB_SOURCES})

target_include_directories(loglite_lib PUBLIC src)
target_link_libraries(loglite_lib PUBLIC
    Boost::headers
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

# ── Tests ─────────────────────────────────────────────────────────────────────

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    SYSTEM
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(tests)
