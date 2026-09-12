#include "tests/simd/shared_consumer.h"

#include "avs_simd/highway_config.h"
#include "avs_simd/target_policy.h"
#include <hwy/detect_targets.h>

extern "C" const char* avs_simd_shared_consumer_target_name(int64_t avs_cpu_flags) {
  return avs_simd::TargetName(avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS));
}
