set(CX_SMOKE_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/avs_core/include")

enable_language(C)
add_library(cx_c_header_check OBJECT c_header_compile.c)
target_include_directories(cx_c_header_check PRIVATE "${CX_SMOKE_INCLUDE_DIR}")

add_library(cx_smoke_dual SHARED
  smoke_plugin/legacy_entry.cpp
  smoke_plugin/legacy_services.cpp
  smoke_plugin/sdk_entry.cpp
  "${CX_SMOKE_INCLUDE_DIR}/avs/cx/legacy.cpp"
  smoke_plugin/checker_kernel.h
)
target_include_directories(cx_smoke_dual PRIVATE "${CX_SMOKE_INCLUDE_DIR}")
set_target_properties(cx_smoke_dual PROPERTIES PREFIX "" OUTPUT_NAME "cx_smoke_dual")

add_library(cx_smoke_legacy SHARED
  smoke_plugin/legacy_entry.cpp
  smoke_plugin/legacy_services.cpp
  smoke_plugin/checker_kernel.h
)
target_include_directories(cx_smoke_legacy PRIVATE "${CX_SMOKE_INCLUDE_DIR}")
set_target_properties(cx_smoke_legacy PROPERTIES PREFIX "" OUTPUT_NAME "cx_smoke_legacy")

add_library(cx_smoke_reject SHARED smoke_plugin/reject_entry.cpp)
target_include_directories(cx_smoke_reject PRIVATE "${CX_SMOKE_INCLUDE_DIR}")
set_target_properties(cx_smoke_reject PROPERTIES PREFIX "" OUTPUT_NAME "cx_smoke_reject")

add_executable(cx_tests cx_tests.cpp)
target_include_directories(cx_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/tests"
  "${CX_SMOKE_INCLUDE_DIR}"
)
target_link_libraries(cx_tests PRIVATE GTest::gtest_main AvsCoreExternal)
target_compile_definitions(cx_tests PRIVATE
  CX_SMOKE_DUAL_PATH="$<TARGET_FILE:cx_smoke_dual>"
  CX_SMOKE_LEGACY_PATH="$<TARGET_FILE:cx_smoke_legacy>"
  CX_SMOKE_REJECT_PATH="$<TARGET_FILE:cx_smoke_reject>"
)
add_dependencies(cx_tests
  cx_c_header_check
  cx_smoke_dual
  cx_smoke_legacy
  cx_smoke_reject
)

include(GoogleTest)
gtest_discover_tests(cx_tests TEST_PREFIX "cx.")

add_executable(cx_smoke_benchmark cx_smoke_benchmark.cpp)
target_include_directories(cx_smoke_benchmark PRIVATE
  "${PROJECT_SOURCE_DIR}/tests"
  "${CX_SMOKE_INCLUDE_DIR}"
)
target_link_libraries(cx_smoke_benchmark PRIVATE AvsCoreExternal)
target_compile_definitions(cx_smoke_benchmark PRIVATE
  CX_SMOKE_DUAL_PATH="$<TARGET_FILE:cx_smoke_dual>"
  CX_SMOKE_LEGACY_PATH="$<TARGET_FILE:cx_smoke_legacy>"
)
add_dependencies(cx_smoke_benchmark cx_smoke_dual cx_smoke_legacy)

target_compile_definitions(cx_tests PRIVATE CX_SMOKE_SDK_PATH="$<TARGET_FILE:cx_smoke_dual>")
add_library(cx_smoke_c SHARED smoke_plugin/c_entry.c)
target_include_directories(cx_smoke_c PRIVATE "${CX_SMOKE_INCLUDE_DIR}")
set_target_properties(cx_smoke_c PROPERTIES PREFIX "" OUTPUT_NAME "cx_smoke_c")
add_dependencies(cx_tests cx_smoke_c)
target_compile_definitions(cx_tests PRIVATE CX_SMOKE_C_PATH="$<TARGET_FILE:cx_smoke_c>")

# A real existing plugin, compiled without editing any of its source.
add_library(cx_convert_stacked SHARED
  "${PROJECT_SOURCE_DIR}/plugins/ConvertStacked/ConvertStacked.cpp"
  smoke_plugin/simple_sdk_entry.cpp
  "${CX_SMOKE_INCLUDE_DIR}/avs/cx/legacy.cpp")
target_include_directories(cx_convert_stacked PRIVATE "${CX_SMOKE_INCLUDE_DIR}")
target_compile_definitions(cx_convert_stacked PRIVATE INTEL_INTRINSICS)
set_target_properties(cx_convert_stacked PROPERTIES PREFIX "")
add_dependencies(cx_tests cx_convert_stacked)
target_compile_definitions(cx_tests PRIVATE CX_STACKED_PATH="$<TARGET_FILE:cx_convert_stacked>")
