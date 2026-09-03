add_executable(greyscale_filter_tests greyscale_filter_tests.cpp)
target_include_directories(greyscale_filter_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
)
target_link_libraries(greyscale_filter_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(greyscale_filter_tests TEST_PREFIX "greyscale_filter.")
