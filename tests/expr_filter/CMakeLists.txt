add_executable(expr_filter_tests expr_filter_tests.cpp)
target_include_directories(expr_filter_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
)
if(ENABLE_INTEL_SIMD)
  target_compile_definitions(expr_filter_tests PRIVATE INTEL_INTRINSICS VS_TARGET_CPU_X86)
endif()
target_link_libraries(expr_filter_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(expr_filter_tests TEST_PREFIX "expr_filter.")
