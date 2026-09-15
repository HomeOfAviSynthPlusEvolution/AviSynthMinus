# Historical kernels are reference-test dependencies only, never AvsCore inputs.
add_library(AvsInternalFilterReferences STATIC turn.cpp)
add_dependencies(AvsInternalFilterReferences VersionGen)
target_compile_definitions(AvsInternalFilterReferences PRIVATE BUILDING_AVSCORE AVS_STATIC_LIB)
target_include_directories(AvsInternalFilterReferences PRIVATE "${CMAKE_SOURCE_DIR}/avs_core"
  "${CMAKE_SOURCE_DIR}/avs_core/include" "${CMAKE_BINARY_DIR}/avs_core")
if(ENABLE_INTEL_SIMD)
  target_sources(AvsInternalFilterReferences PRIVATE intel/focus_sse.cpp intel/focus_avx2.cpp
    intel/turn_sse.cpp intel/limiter_sse.cpp intel/planeswap_sse.cpp)
  if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(intel/focus_avx2.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX2)
  else()
    set_source_files_properties(intel/focus_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
  endif()
endif()
