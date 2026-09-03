add_executable(support_tests tests/support_tests.cpp)
target_include_directories(support_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(support_tests PRIVATE GTest::gtest_main AvsCoreExternal)

include(GoogleTest)
gtest_discover_tests(support_tests TEST_PREFIX "support.")
