add_executable(audio_filter_tests audio_filter_tests.cpp convert_audio_api_tests.cpp)
if(ENABLE_INTEL_SIMD)
  target_compile_definitions(audio_filter_tests PRIVATE INTEL_INTRINSICS)
endif()
target_include_directories(audio_filter_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${CMAKE_SOURCE_DIR}/modules/audio_convert/include
)
target_link_libraries(audio_filter_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(audio_filter_tests TEST_PREFIX "audio_filter.")
