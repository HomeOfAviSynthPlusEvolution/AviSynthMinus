add_executable(convert_bits_tests
  convert_bits_integer_tests.cpp
  convert_bits_dither_tests.cpp
  convert_bits_float_tests.cpp
)
target_include_directories(convert_bits_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(convert_bits_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(convert_bits_tests TEST_PREFIX "convert_bits.")
