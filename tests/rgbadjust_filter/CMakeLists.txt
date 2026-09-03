add_executable(rgbadjust_filter_tests rgbadjust_filter_tests.cpp)
target_include_directories(rgbadjust_filter_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
)
target_link_libraries(rgbadjust_filter_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(rgbadjust_filter_tests TEST_PREFIX "rgbadjust_filter.")
