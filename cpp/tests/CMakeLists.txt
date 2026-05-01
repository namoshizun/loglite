find_package(GTest REQUIRED)

file(GLOB test_sources CONFIGURE_DEPENDS "test_*.cpp")
add_executable(loglite_tests ${test_sources})

target_link_libraries(loglite_tests PRIVATE loglite_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(loglite_tests)

# ── Optional coverage report (lcov) ───────────────────────────────────────────
#
# Controlled by -DLOGLITE_COVERAGE=ON, which is set in the parent CMakeLists.
# The parent already instruments loglite_lib; here we only instrument the test
# binary itself and define the loglite_coverage reporting target.
#
# Two-pass capture (--initial baseline + actual run) ensures every instrumented
# source file appears in the report — even TUs whose symbols are not linked into
# loglite_tests (e.g. globals.cpp) show up at 0% instead of being absent.
#
# lcov -q -q suppresses the "Reading tracefile …" / "Message summary" noise
# lines without needing a helper script or shell pipe.

if(LOGLITE_COVERAGE)
    target_compile_options(loglite_tests PRIVATE --coverage)
    target_link_options(loglite_tests    PRIVATE --coverage)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(loglite_tests PRIVATE -fprofile-update=atomic)
    endif()

    find_program(LOGLITE_LCOV_EXECUTABLE lcov REQUIRED)

    set(_cov_baseline "${CMAKE_BINARY_DIR}/coverage_baseline.info")
    set(_cov_raw      "${CMAKE_BINARY_DIR}/coverage_raw.info")
    set(_cov_merged   "${CMAKE_BINARY_DIR}/coverage_merged.info")
    set(_cov_info     "${CMAKE_BINARY_DIR}/coverage.info")

    set(_lcov_ignore
        --ignore-errors inconsistent,inconsistent,unsupported,unsupported,mismatch,mismatch,range,range,format,format
    )
    set(_lcov_exclude
        "/usr/*"
        "/Library/*"
        "/opt/homebrew/*"
        "*/_deps/*"
        "*/.conan/*"
        "*/.conan2/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/*"
    )

    add_custom_target(loglite_coverage
        # 1. Wipe stale intermediate files and zero all counters.
        COMMAND ${CMAKE_COMMAND} -E rm -f
                "${_cov_baseline}" "${_cov_raw}" "${_cov_merged}" "${_cov_info}"
        COMMAND ${LOGLITE_LCOV_EXECUTABLE} -q -q
                --zerocounters --directory "${CMAKE_BINARY_DIR}"

        # 2. Baseline from .gcno files — every instrumented line starts at 0%
        #    so source files not exercised by any test still appear in the report.
        COMMAND ${LOGLITE_LCOV_EXECUTABLE} -q -q
                --capture --initial
                --directory "${CMAKE_BINARY_DIR}"
                ${_lcov_ignore} --keep-going
                --output-file "${_cov_baseline}"

        # 3. Run the full test suite (writes .gcda execution counters).
        COMMAND $<TARGET_FILE:loglite_tests>

        # 4. Capture actual execution counters.
        COMMAND ${LOGLITE_LCOV_EXECUTABLE} -q -q
                --capture
                --directory "${CMAKE_BINARY_DIR}"
                ${_lcov_ignore} --keep-going
                --output-file "${_cov_raw}"

        # 5. Merge baseline + actual.
        COMMAND ${LOGLITE_LCOV_EXECUTABLE} -q -q
                --add-tracefile "${_cov_baseline}"
                --add-tracefile "${_cov_raw}"
                --ignore-errors inconsistent,inconsistent,range,range,format,format
                --keep-going
                --output-file "${_cov_merged}"

        # 6. Keep only project sources (drop system, Conan, FetchContent, tests/).
        COMMAND ${LOGLITE_LCOV_EXECUTABLE} -q -q
                --remove "${_cov_merged}"
                ${_lcov_exclude}
                --ignore-errors unused,unused,inconsistent,inconsistent,unsupported,range,format
                --keep-going
                --output-file "${_cov_info}"

        # 7. Print summary + per-file table.
        COMMAND ${LOGLITE_LCOV_EXECUTABLE}
                -q -q
                --ignore-errors inconsistent,inconsistent,corrupt,corrupt
                --summary "${_cov_info}"
        COMMAND ${LOGLITE_LCOV_EXECUTABLE}
                -q -q
                --ignore-errors inconsistent,inconsistent,corrupt,corrupt
                --list "${_cov_info}"

        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        DEPENDS loglite_tests
        COMMENT "Running tests and printing lcov coverage to the terminal..."
        USES_TERMINAL
    )
endif()
