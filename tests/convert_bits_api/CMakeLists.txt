add_executable(convert_bits_api_tests convert_bits_api_tests.cpp convert_bits_dither_api_tests.cpp)
target_compile_features(convert_bits_api_tests PRIVATE cxx_std_17)
target_compile_definitions(convert_bits_api_tests PRIVATE DEPTH_FIXTURE="${PROJECT_SOURCE_DIR}/third_party/video_convert/tests/fixtures/depth/upstream-c.txt")
target_link_libraries(convert_bits_api_tests PRIVATE GTest::gtest_main AvsCoreExternal)
include(GoogleTest)
gtest_discover_tests(convert_bits_api_tests TEST_PREFIX "convert_bits_api." NO_PRETTY_VALUES DISCOVERY_TIMEOUT 30)

target_compile_definitions(convert_bits_api_tests PRIVATE
  ORDERED_FIXTURE="${PROJECT_SOURCE_DIR}/third_party/video_convert/tests/fixtures/dither/corrected-c.txt"
  FLOYD_FIXTURE="${PROJECT_SOURCE_DIR}/third_party/video_convert/tests/fixtures/dither/floyd-upstream-c.txt")
