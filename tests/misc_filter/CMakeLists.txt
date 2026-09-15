add_executable(misc_filter_tests misc_filter_tests.cpp)
target_link_libraries(misc_filter_tests PRIVATE GTest::gtest_main AvsCoreExternal)
include(GoogleTest)
gtest_discover_tests(misc_filter_tests TEST_PREFIX "misc_filter.")
