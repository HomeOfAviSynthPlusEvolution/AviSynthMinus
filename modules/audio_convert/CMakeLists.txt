# AVS owns the filter adapter and CPU policy; conversion kernels live in the
# independent AudioConvert submodule. Both dependencies reuse the host hwy target.
add_library(AvsAudioConvert STATIC
  "${CMAKE_CURRENT_LIST_DIR}/src/filter.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/script.cpp"
)
set_target_properties(AvsAudioConvert PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(AvsAudioConvert PRIVATE cxx_std_17)
target_compile_definitions(AvsAudioConvert PRIVATE BUILDING_AVSCORE)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(AvsAudioConvert PRIVATE AVS_STATIC_LIB)
endif()
target_link_libraries(AvsAudioConvert PRIVATE AviSynth::ConvertAudio AvsSimd)
target_include_directories(AvsAudioConvert PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}/include"
  "${CMAKE_CURRENT_LIST_DIR}/src"
)
