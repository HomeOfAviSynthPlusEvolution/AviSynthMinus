# These direct kernel tests require the x86 implementations in AvsCore.
# Public Limiter coverage remains enabled in tests/limiter_filter on every target.
if(NOT ENABLE_INTEL_SIMD)
  return()
endif()

add_executable(limiter_tests limiter_tests.cpp)
target_include_directories(limiter_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(limiter_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
  AvsInternalFilterReferences
)

include(GoogleTest)
gtest_discover_tests(limiter_tests TEST_PREFIX "limiter.")
