add_executable(internal_filter_registration_tests registration_tests.cpp)
target_link_libraries(internal_filter_registration_tests PRIVATE GTest::gtest_main AvsCoreExternal)
include(GoogleTest)
gtest_discover_tests(internal_filter_registration_tests TEST_PREFIX "internal_filters.")

add_executable(internal_filter_simd_tests simd_tests.cpp)
target_link_libraries(internal_filter_simd_tests PRIVATE GTest::gtest_main
  AvsCoreExternal AvsFilters::FocusKernels AvsFilters::RotationKernels
  AvsFilters::LimiterKernels)
# Test the requested configuration, including when legacy x86 SIMD is disabled.
target_compile_definitions(internal_filter_simd_tests PRIVATE
  AVS_TEST_AIF_SCALAR_ONLY=$<BOOL:$CACHE{AIF_SCALAR_ONLY}>)
gtest_discover_tests(internal_filter_simd_tests TEST_PREFIX "internal_filters.")
