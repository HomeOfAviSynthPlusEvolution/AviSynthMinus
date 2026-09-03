add_executable(merge_tests
  merge_tests.cpp
  merge_float_tests.cpp
  merge_additional_tests.cpp
)
target_include_directories(merge_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(merge_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(merge_tests TEST_PREFIX "merge.")
