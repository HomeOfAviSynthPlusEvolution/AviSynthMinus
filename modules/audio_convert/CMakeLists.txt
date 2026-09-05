set(AudioConvertKernels_Sources
  "${CMAKE_CURRENT_LIST_DIR}/src/kernels_scalar.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/kernels_highway.cpp"
)

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
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/avs_core/include>
  )
endforeach()

target_link_libraries(AudioConvertKernelObjects PRIVATE AvsSimd)
target_link_libraries(AudioConvertKernels PUBLIC AvsSimd)

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
