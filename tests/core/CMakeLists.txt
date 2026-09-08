add_executable(core_tests
  script_tests.cpp
  fps_tests.cpp
  config_tests.cpp
  c_api_tests.cpp
  environment_tests.cpp
  cache_tests.cpp
  plugin_tests.cpp
  import_tests.cpp
  animate_tests.cpp
  source_tests.cpp
  greyscale_tests.cpp
)
target_include_directories(core_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(core_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(core_tests TEST_PREFIX "core.")

# Keep the replacement allocator isolated from the other core tests. Each case
# runs in its own process so a broken lock cannot hang the entire test run.
add_executable(global_lock_tests global_lock_tests.cpp)
target_link_libraries(global_lock_tests PRIVATE GTest::gtest_main AvsCoreExternal)
gtest_discover_tests(global_lock_tests TEST_PREFIX "core."
  PROPERTIES TIMEOUT 15)
