add_executable(turn_tests
  turn_tests.cpp
  turn_rgb_tests.cpp
  turn_180_tests.cpp
)
target_include_directories(turn_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(turn_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(turn_tests TEST_PREFIX "turn.")
