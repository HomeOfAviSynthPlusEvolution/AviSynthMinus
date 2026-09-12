enable_language(C)
if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|FreeBSD)$")
  # Match the symbol isolation used for the shipped plugins.
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,-Bsymbolic")
endif()
include(FetchContent)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_SHALLOW TRUE
)
FetchContent_Declare(
  xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG v0.8.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest xxhash)

add_library(AvsCoreExternal INTERFACE)
target_link_libraries(AvsCoreExternal INTERFACE AvsCore)
target_compile_definitions(AvsCoreExternal INTERFACE AVS_STATIC_LIB)
target_include_directories(AvsCoreExternal INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/avs_core/include
  ${CMAKE_SOURCE_DIR}/avs_core
  ${CMAKE_BINARY_DIR}/avs_core
  ${xxhash_SOURCE_DIR}
)

add_subdirectory(support)
add_subdirectory(core)
add_subdirectory(cx)
add_subdirectory(v12)
add_subdirectory(bitblt)
add_subdirectory(limiter)
add_subdirectory(overlay)
add_subdirectory(overlay_filter)
add_subdirectory(planeswap)
add_subdirectory(greyscale)
add_subdirectory(focus)
# add_subdirectory(layer) -- tests 3.7.6-dev new Layer placement and AVX2 features
# add_subdirectory(layer_filter) -- tests 3.7.6-dev new Layer placement features
# add_subdirectory(merge) -- tests 3.7.6-dev unified merge kernel API
add_subdirectory(turn)
add_subdirectory(resize)
add_subdirectory(resize_filter)
add_subdirectory(transform_filter)
add_subdirectory(conditional)
add_subdirectory(convert_bits)
add_subdirectory(convert_matrix)
add_subdirectory(convert_planar)
add_subdirectory(convert_rgb)
add_subdirectory(convert_rgb_filter)
add_subdirectory(text_overlay)
add_subdirectory(avi_helper)
# Kernel tests are supplied by the AudioConvert submodule.
add_subdirectory(audio_filter)
add_subdirectory(simd)
add_subdirectory(convolution)
add_subdirectory(levels)
add_subdirectory(color)
add_subdirectory(histogram)
add_subdirectory(field_filter)
add_subdirectory(limiter_filter)
add_subdirectory(greyscale_filter)
add_subdirectory(planeswap_filter)
add_subdirectory(turn_filter)
add_subdirectory(merge_filter)
add_subdirectory(focus_filter)
add_subdirectory(tweak_filter)
add_subdirectory(rgbadjust_filter)
add_subdirectory(expr_filter)
add_subdirectory(findings)
