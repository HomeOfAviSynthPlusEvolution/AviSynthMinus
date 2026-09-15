# Direct x86 kernel tests require the matching AvsCore implementations.
if(NOT ENABLE_INTEL_SIMD)
  return()
endif()

add_executable(planeswap_tests planeswap_tests.cpp)
target_include_directories(planeswap_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(planeswap_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
  AvsInternalFilterReferences
)

include(GoogleTest)
gtest_discover_tests(planeswap_tests TEST_PREFIX "planeswap.")
