cmake_minimum_required(VERSION 3.25)
project(loglite_python CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Conan dependency management ────────────────────────────────────────────────
#
# Two modes:
#
#   1. Automatic (default, e.g. `uv sync` / `pip install -e .`):
#      No CMAKE_TOOLCHAIN_FILE is set, so Conan is invoked here during CMake
#      configure.  Conan's per-user binary cache makes repeated runs fast.
#      Requires `conan` on PATH; install it with `pip install conan`.
#
#   2. Pre-supplied toolchain (e.g. cibuildwheel CI):
#      Pass -DCMAKE_TOOLCHAIN_FILE=<conan_output>/conan_toolchain.cmake.
#      Conan is assumed to have been run as a prior step (CIBW_BEFORE_ALL).
#
if(DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    # Deps already installed; CMakeDeps files live alongside the toolchain.
    get_filename_component(_conan_output_dir "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    list(PREPEND CMAKE_PREFIX_PATH "${_conan_output_dir}")
    list(PREPEND CMAKE_MODULE_PATH "${_conan_output_dir}")
else()
    find_program(CONAN_EXE conan
        REQUIRED
        DOC "Conan package manager — install with: pip install conan"
    )
    message(STATUS "[loglite] Running Conan install "
                   "(first run compiles deps; subsequent runs use the cache)...")
    execute_process(
        COMMAND
            ${CONAN_EXE} install ${CMAKE_CURRENT_SOURCE_DIR}/../cpp
            --output-folder=${CMAKE_CURRENT_BINARY_DIR}
            -o with_python=True
            -o with_tests=False
            -s build_type=${CMAKE_BUILD_TYPE}
            --build=missing
        RESULT_VARIABLE _conan_exit
        COMMAND_ERROR_IS_FATAL ANY
    )
    # Only the prefix path matters for find_package — we don't include
    # conan_toolchain.cmake because doing so after project() would clobber
    # compiler and extension settings (CMAKE_CXX_EXTENSIONS etc.) that are
    # already correctly configured above.
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_BINARY_DIR}")
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_BINARY_DIR}")
endif()

find_package(pybind11 REQUIRED CONFIG)

set(LOGLITE_PYTHON ON CACHE BOOL "" FORCE)
set(BUILD_TESTING  OFF CACHE BOOL "" FORCE)
add_subdirectory(../cpp ${CMAKE_CURRENT_BINARY_DIR}/cpp)

install(TARGETS _core LIBRARY DESTINATION loglite)
