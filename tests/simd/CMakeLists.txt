# 1. Independent policy test target without AvsCore dependency
add_executable(simd_policy_tests
  target_policy_tests.cpp
)

target_include_directories(simd_policy_tests PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(simd_policy_tests PRIVATE
  AvsSimd
  GTest::gtest_main
)

# 2. Comprehensive SIMD multi-target dispatch and environment isolation test target
add_library(simd_shared_consumer SHARED
  shared_consumer.cpp
)

target_include_directories(simd_shared_consumer PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(simd_shared_consumer PRIVATE AVS_SIMD_SHARED_CONSUMER_BUILD)

target_link_libraries(simd_shared_consumer PRIVATE AvsSimd)

add_executable(simd_tests
  dispatch_probe.cpp
  dispatch_probe_consumer.cpp
  dispatch_tests.cpp
)

target_include_directories(simd_tests PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
)

target_link_libraries(simd_tests PRIVATE
  AvsSimd
  AvsCoreExternal
  simd_shared_consumer
  GTest::gtest_main
)

# Ensure building simd_tests also builds the standalone policy test target
add_dependencies(simd_tests simd_policy_tests)

include(GoogleTest)
gtest_discover_tests(simd_policy_tests TEST_PREFIX "simd.")
gtest_discover_tests(simd_tests TEST_PREFIX "simd.")
