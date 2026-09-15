add_library(AvsInternalFilters STATIC registration.cpp)
set_target_properties(AvsInternalFilters PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(AvsInternalFilters PUBLIC cxx_std_17)
target_compile_definitions(AvsInternalFilters PRIVATE BUILDING_AVSCORE)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(AvsInternalFilters PRIVATE AVS_STATIC_LIB)
endif()
target_include_directories(AvsInternalFilters PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/third_party/internal_filters/src"
  PRIVATE "${CMAKE_BINARY_DIR}/avs_core" "${CMAKE_SOURCE_DIR}/avs_core" "${CMAKE_SOURCE_DIR}/avs_core/include")
target_link_libraries(AvsInternalFilters PUBLIC
  aif_blank_clip_filters
  aif_channel_display_filters
  aif_color_adjust_filters
  aif_color_bars_filters
  aif_convolution_filters
  aif_crop_filters
  aif_focus_filters
  aif_greyscale_filters
  aif_invert_filters
  aif_limiter_filters
  aif_merge_filters
  aif_planes_filters
  aif_rgb_merge_filters
  aif_rotation_filters
  aif_rows_columns_filters
  aif_stack_filters
)
