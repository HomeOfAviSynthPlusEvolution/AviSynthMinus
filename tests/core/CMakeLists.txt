add_executable(core_tests
  script_tests.cpp
)
target_include_directories(core_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(core_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(core_tests TEST_PREFIX "core.")
