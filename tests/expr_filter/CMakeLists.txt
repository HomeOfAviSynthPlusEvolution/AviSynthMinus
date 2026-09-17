if(NOT ENABLE_IRIS)
  return()
endif()

add_executable(expr_filter_tests expr_filter_tests.cpp)
target_include_directories(expr_filter_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
  ${PROJECT_SOURCE_DIR}/avs_core/include
  ${PROJECT_SOURCE_DIR}/avs_core
  ${PROJECT_BINARY_DIR}/avs_core
)
if(IRIS_LLVM)
  target_compile_definitions(expr_filter_tests PRIVATE AVS_TEST_IRIS_LLVM=1)
endif()
if(IRIS_LLVM AND IRIS_SLEEF)
  target_compile_definitions(expr_filter_tests PRIVATE AVS_TEST_IRIS_SLEEF=1)
endif()
target_link_libraries(expr_filter_tests PRIVATE
  GTest::gtest_main
  AvsCoreExternal
)

include(GoogleTest)
gtest_discover_tests(expr_filter_tests TEST_PREFIX "expr_filter.")

target_sources(expr_filter_tests PRIVATE expr_boundary_tests.cpp)

# These tests call the private native factory, which is not a DLL export.
if(ENABLE_IRIS AND NOT BUILD_SHARED_LIBS)
  add_executable(iris_filter_tests iris_filter_tests.cpp)
  target_include_directories(iris_filter_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/modules/iris/include")
  target_link_libraries(iris_filter_tests PRIVATE GTest::gtest_main AvsCoreExternal)
  if(IRIS_LLVM)
    target_compile_definitions(iris_filter_tests PRIVATE AVS_TEST_IRIS_LLVM=1)
  endif()
  if(IRIS_SLEEF AND IRIS_LLVM)
    target_compile_definitions(iris_filter_tests PRIVATE AVS_TEST_IRIS_SLEEF=1)
  endif()
  gtest_discover_tests(iris_filter_tests TEST_PREFIX "iris_filter.")
endif()
