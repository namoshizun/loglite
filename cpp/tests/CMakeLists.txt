file(GLOB test_sources CONFIGURE_DEPENDS "test_*.cpp")
add_executable(loglite_tests ${test_sources})

target_link_libraries(loglite_tests PRIVATE loglite_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(loglite_tests)
