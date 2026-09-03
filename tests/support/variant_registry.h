#pragma once

#include "support/cpu_features.h"

#include <string>

namespace avsut::test {

template <typename Function>
struct Variant {
  std::string name;
  Function function;
  IsaRequirement requirement;
};

template <typename Function>
bool variant_supported(const Variant<Function>& variant, CpuFeatures features) noexcept {
  return features.supports(variant.requirement);
}

}  // namespace avsut::test
