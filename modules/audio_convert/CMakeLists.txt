set(AudioConvertKernels_Sources
  "${CMAKE_CURRENT_LIST_DIR}/src/kernels_scalar.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/kernels_highway.cpp"
)

# Each archive owns its implementation; CMake carries the link dependencies.
add_library(AudioConvertKernels STATIC ${AudioConvertKernels_Sources})

set_target_properties(AudioConvertKernels PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

target_include_directories(AudioConvertKernels PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/avs_core/include>
)
target_link_libraries(AudioConvertKernels PUBLIC AvsSimd)

# AviSynth adapters are internal to the core build, not a plugin SDK binary.
if(TARGET AvsCore)
  add_library(AudioConvert STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/filter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/script.cpp"
  )
  set_target_properties(AudioConvert PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(AudioConvert PRIVATE BUILDING_AVSCORE)
  if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(AudioConvert PRIVATE AVS_STATIC_LIB)
  endif()
  target_link_libraries(AudioConvert PRIVATE AudioConvertKernels)
  target_include_directories(AudioConvert PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/src"
  )
endif()
