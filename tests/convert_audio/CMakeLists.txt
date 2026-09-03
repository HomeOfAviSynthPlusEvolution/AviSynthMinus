add_executable(convert_audio_tests convert_audio_tests.cpp)
target_include_directories(convert_audio_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)
target_compile_definitions(convert_audio_tests PRIVATE INTEL_INTRINSICS)
target_link_libraries(convert_audio_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(convert_audio_tests TEST_PREFIX "convert_audio.")
