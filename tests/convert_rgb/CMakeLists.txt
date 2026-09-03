add_executable(convert_rgb_tests
  convert_rgb_tests.cpp
  convert_rgb_planar_tests.cpp
  convert_rgb_packed_tests.cpp
)
target_include_directories(convert_rgb_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(convert_rgb_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(convert_rgb_tests TEST_PREFIX "convert_rgb.")
