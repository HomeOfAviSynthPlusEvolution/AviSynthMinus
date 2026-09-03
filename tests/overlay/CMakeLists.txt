add_executable(overlay_tests
  overlay_multiply_tests.cpp
  overlay_lighten_darken_tests.cpp
)
target_include_directories(overlay_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(overlay_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(overlay_tests TEST_PREFIX "overlay.")
