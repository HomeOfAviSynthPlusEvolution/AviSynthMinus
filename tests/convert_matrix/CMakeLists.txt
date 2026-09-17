add_executable(convert_matrix_tests
  convert_matrix_properties_tests.cpp
  convert_matrix_parser_tests.cpp
  convert_matrix_gain_tests.cpp
  convert_matrix_planar_tests.cpp
  convert_matrix_packed_tests.cpp
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

# Match VideoConvert's separate multiply/add rounding when applying the fixed reference coefficients.
# A fused reference can differ by one ULP even when the kernel is correct.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  if(MSVC)
    set_source_files_properties(convert_matrix_planar_tests.cpp PROPERTIES COMPILE_OPTIONS /clang:-ffp-contract=off)
  else()
    set_source_files_properties(convert_matrix_planar_tests.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
  endif()
endif()
