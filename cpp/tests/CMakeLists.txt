file(GLOB test_sources CONFIGURE_DEPENDS "test_*.cpp")
add_executable(loglite_tests ${test_sources})

target_link_libraries(loglite_tests PRIVATE loglite_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(loglite_tests)

# ── Optional coverage (lcov) ────────────────────────────────────────────────────
#
# Configure with: cmake -B build -DLOGLITE_COVERAGE=ON
# Build & report: cmake --build build --target loglite_coverage
# After toggling LOGLITE_COVERAGE or changing sources, prefer a clean rebuild
# so .gcno/.gcda match (avoids stale line-range noise under Clang/lcov).
#
# Runs tests, collects gcda data, filters out system deps and unit-test harness
# sources, then prints a summary plus per-file coverage for library code under
# src/.
#
# lcov is picky about Clang/LLVM gcov data; the suppress flags below keep the
# target usable without noisy failures.

option(LOGLITE_COVERAGE "Instrument loglite_lib + tests; use target loglite_coverage" OFF)

if(LOGLITE_COVERAGE)
    target_compile_options(loglite_lib PRIVATE --coverage)
    target_compile_options(loglite_tests PRIVATE --coverage)
    target_link_options(loglite_tests PRIVATE --coverage)
    target_link_options(loglite PRIVATE --coverage)

    find_program(LOGLITE_LCOV_EXECUTABLE lcov REQUIRED)

    set(_cov_raw "${CMAKE_BINARY_DIR}/coverage_raw.info")
    set(_cov_info "${CMAKE_BINARY_DIR}/coverage.info")

    add_custom_target(loglite_coverage
        COMMAND ${CMAKE_COMMAND} -E rm -f "${_cov_raw}" "${_cov_info}"
        COMMAND ${LOGLITE_LCOV_EXECUTABLE} --zerocounters --directory "${CMAKE_BINARY_DIR}"
        COMMAND $<TARGET_FILE:loglite_tests>
        COMMAND ${LOGLITE_LCOV_EXECUTABLE}
                --capture
                --directory "${CMAKE_BINARY_DIR}"
                --ignore-errors inconsistent,unsupported,mismatch,range,format
                --keep-going
                --output-file "${_cov_raw}"
        COMMAND ${LOGLITE_LCOV_EXECUTABLE}
                --remove "${_cov_raw}"
                "/usr/*"
                "/Library/*"
                "/opt/homebrew/*"
                "*/_deps/*"
                "${CMAKE_CURRENT_SOURCE_DIR}/*"
                --ignore-errors unused,inconsistent,unsupported,range,format
                --keep-going
                --output-file "${_cov_info}"
        COMMAND ${LOGLITE_LCOV_EXECUTABLE} --ignore-errors inconsistent,corrupt
                --summary "${_cov_info}"
        COMMAND ${LOGLITE_LCOV_EXECUTABLE} --ignore-errors inconsistent,corrupt
                --list "${_cov_info}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        DEPENDS loglite_tests
        COMMENT "Running tests and printing lcov coverage to the terminal..."
        USES_TERMINAL
    )
endif()
