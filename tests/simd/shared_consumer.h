#ifndef TESTS_SIMD_SHARED_CONSUMER_H
#define TESTS_SIMD_SHARED_CONSUMER_H

#if defined(_WIN32)
  #if defined(AVS_SIMD_SHARED_CONSUMER_BUILD)
    #define AVS_SIMD_SHARED_API __declspec(dllexport)
  #else
    #define AVS_SIMD_SHARED_API __declspec(dllimport)
  #endif
#else
  #define AVS_SIMD_SHARED_API __attribute__((visibility("default")))
#endif

extern "C" AVS_SIMD_SHARED_API const char* avs_simd_shared_consumer_target_name(int avs_cpu_flags);

#endif  // TESTS_SIMD_SHARED_CONSUMER_H
