add_executable(convert_planar_tests
  convert_to_planar_tests.cpp
  convert_yv24_rgb_tests.cpp
)
target_include_directories(convert_planar_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(convert_planar_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(convert_planar_tests TEST_PREFIX "convert_planar.")
