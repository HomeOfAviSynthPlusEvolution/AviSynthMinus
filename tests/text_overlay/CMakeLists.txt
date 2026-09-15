add_executable(compare_filter_tests compare_filter_tests.cpp)
target_link_libraries(compare_filter_tests PRIVATE GTest::gtest_main AvsCoreExternal)
include(GoogleTest)
gtest_discover_tests(compare_filter_tests TEST_PREFIX "text_overlay_filter.")

# Direct x86 kernel tests require the matching AvsCore implementations.
if(NOT ENABLE_INTEL_SIMD)
  return()
endif()

add_executable(text_overlay_tests
  text_overlay_compare_tests.cpp
)
target_include_directories(text_overlay_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(text_overlay_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(text_overlay_tests TEST_PREFIX "text_overlay.")
