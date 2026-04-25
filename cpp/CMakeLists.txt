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
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    SYSTEM
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    SYSTEM
)

FetchContent_MakeAvailable(yaml-cpp nlohmann_json CLI11)

# ── Main library target ───────────────────────────────────────────────────────

add_library(loglite_lib STATIC
    src/config.cpp
    src/database.cpp
    src/column_dict.cpp
    src/migrations.cpp
    src/server.cpp
)

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
