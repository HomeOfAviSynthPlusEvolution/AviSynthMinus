set(HWY_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/highway")

if(NOT EXISTS "${HWY_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Vendored Highway sources not found in ${HWY_SOURCE_DIR}. Highway must be present in third_party/highway and cannot be downloaded during build.")
endif()

# Configure vendored upstream options before inclusion
set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "Include contrib/" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "Enable HWY tests" FORCE)
set(HWY_ENABLE_TOOLS OFF CACHE BOOL "Build tools" FORCE)
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "Install library" FORCE)
set(HWY_FORCE_STATIC_LIBS ON CACHE BOOL "Ignore BUILD_SHARED_LIBS" FORCE)
# Do not let Highway turn RVV into a project-wide compiler or linker baseline.
# Runtime-dispatch-capable compilers may still expose RVV targets themselves.
set(HWY_CMAKE_RVV OFF CACHE BOOL "Set copts for RISCV with RVV?" FORCE)

add_subdirectory("${HWY_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/highway" EXCLUDE_FROM_ALL)

# Common AviSynth+ SIMD target
add_library(AvsSimd STATIC EXCLUDE_FROM_ALL
  "${CMAKE_CURRENT_LIST_DIR}/src/target_policy.cpp"
)

target_compile_features(AvsSimd PUBLIC cxx_std_17)

set_target_properties(AvsSimd PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

target_include_directories(AvsSimd
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/avs_core/include>
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
)

# Consumers link AvsSimd and transitively receive Highway core runtime headers and library
target_link_libraries(AvsSimd PUBLIC hwy)
