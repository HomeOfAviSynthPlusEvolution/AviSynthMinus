set(AudioConvertKernels_Sources
  "${CMAKE_CURRENT_LIST_DIR}/src/kernels_scalar.cpp"
)

if(ENABLE_INTEL_SIMD)
  list(APPEND AudioConvertKernels_Sources
    "${CMAKE_CURRENT_LIST_DIR}/src/intel/kernels_sse.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/intel/kernels_avx2.cpp"
  )

  if(MSVC_IDE)
    if(CLANG_IN_VS STREQUAL "1" OR IntelLLVM_IN_VS STREQUAL "1")
      set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/src/intel/kernels_avx2.cpp" PROPERTIES COMPILE_FLAGS " -mavx2 -mfma ")
    else()
      set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/src/intel/kernels_avx2.cpp" PROPERTIES COMPILE_FLAGS " /arch:AVX2 ")
    endif()
  elseif(MSVC)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/src/intel/kernels_avx2.cpp" PROPERTIES COMPILE_FLAGS " -mavx2 -mfma ")
    else()
      set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/src/intel/kernels_avx2.cpp" PROPERTIES COMPILE_FLAGS " /arch:AVX2 ")
    endif()
  else()
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/src/intel/kernels_avx2.cpp" PROPERTIES COMPILE_FLAGS "-mavx2 -mfma")
  endif()
endif()

# Share objects so the installed static core remains self-contained, while
# kernel tests can link a separate archive without depending on AvsCore.
add_library(AudioConvertKernelObjects OBJECT ${AudioConvertKernels_Sources})
add_library(AudioConvertKernels STATIC $<TARGET_OBJECTS:AudioConvertKernelObjects>)

set_target_properties(AudioConvertKernelObjects PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

foreach(audio_target AudioConvertKernelObjects AudioConvertKernels)
  target_include_directories(${audio_target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/avs_core/include>
  )

  if(ENABLE_INTEL_SIMD)
    target_compile_definitions(${audio_target} PUBLIC INTEL_INTRINSICS)
  endif()
endforeach()

# Adapters into AvsCore
if(TARGET AvsCore)
  target_sources(AvsCore PRIVATE
    $<TARGET_OBJECTS:AudioConvertKernelObjects>
    "${CMAKE_CURRENT_LIST_DIR}/src/filter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/script.cpp"
  )
  target_include_directories(AvsCore PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/src"
  )
endif()
