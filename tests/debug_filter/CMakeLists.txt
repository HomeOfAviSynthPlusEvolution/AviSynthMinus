add_executable(debug_filter_tests debug_filter_tests.cpp)
target_link_libraries(debug_filter_tests PRIVATE GTest::gtest_main AvsCoreExternal)
include(GoogleTest)
gtest_discover_tests(debug_filter_tests TEST_PREFIX "debug_filter.")
