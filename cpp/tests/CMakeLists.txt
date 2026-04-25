add_executable(loglite_tests
    test_config.cpp
    test_database.cpp
    test_query_filters.cpp
)

target_link_libraries(loglite_tests PRIVATE loglite_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(loglite_tests)
