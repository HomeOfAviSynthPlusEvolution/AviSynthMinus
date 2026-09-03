add_executable(convert_matrix_tests
  convert_matrix_properties_tests.cpp
  convert_matrix_parser_tests.cpp
)
target_include_directories(convert_matrix_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
)
target_link_libraries(convert_matrix_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(convert_matrix_tests TEST_PREFIX "convert_matrix.")
