#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace avsut::test {
struct KernelCpuProfile {
  std::string name;
  uint32_t cpu;
};
// Always exercise scalar and the host's available backend, plus the requested
// compatibility profile when available. Unsupported ISA requests are not tests
// of that ISA merely because dispatch silently falls back to scalar.
inline std::vector<uint32_t> kernel_cpu_profiles(uint32_t requested, uint32_t supported) {
  std::vector<uint32_t> profiles{0};
  for (const auto cpu : {requested & supported, supported})
    if (std::find(profiles.begin(), profiles.end(), cpu) == profiles.end()) profiles.push_back(cpu);
  return profiles;
}
}  // namespace avsut::test
