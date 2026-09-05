add_executable(convert_audio_tests convert_audio_tests.cpp)
target_include_directories(convert_audio_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${xxhash_SOURCE_DIR}
)
target_link_libraries(convert_audio_tests PRIVATE
  GTest::gtest_main
  AudioConvertKernels
)

include(GoogleTest)
gtest_discover_tests(convert_audio_tests TEST_PREFIX "convert_audio.")
