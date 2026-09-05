add_executable(finding_tests
  finding_b1_convert_tests.cpp
  finding_b2_convert_tests.cpp
  finding_b3_resize_tests.cpp
  finding_b4_planeswap_tests.cpp
  finding_b5_filters_tests.cpp
  finding_b6_color_tests.cpp
  finding_b7_merge_layer_tests.cpp
  finding_b8_overlay_tests.cpp
  finding_b9_conditional_tests.cpp
  finding_b10_expr_tests.cpp
  finding_b11_source_tests.cpp
  finding_b11_misc_tests.cpp
  finding_b11_debug_tests.cpp
  finding_b11_histogram_tests.cpp
  finding_b11_text_overlay_tests.cpp
)
target_include_directories(finding_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
  ${CMAKE_SOURCE_DIR}/modules/audio_convert/include
)
target_compile_definitions(finding_tests PRIVATE INTEL_INTRINSICS VS_TARGET_CPU_X86)
target_link_libraries(finding_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
  AudioConvertKernels
)

include(GoogleTest)
gtest_discover_tests(finding_tests TEST_PREFIX "finding.")
