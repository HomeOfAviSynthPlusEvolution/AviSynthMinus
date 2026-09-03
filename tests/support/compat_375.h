#pragma once

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#endif

#include "convert/convert_helper.h"

struct ColorRange_Compat_e {
  static constexpr int AVS_COLORRANGE_FULL = ColorRange_e::AVS_RANGE_FULL;
  static constexpr int AVS_COLORRANGE_LIMITED = ColorRange_e::AVS_RANGE_LIMITED;
};

#ifndef AVS_COLORRANGE_FULL
inline constexpr int AVS_COLORRANGE_FULL = ColorRange_e::AVS_RANGE_FULL;
inline constexpr int AVS_COLORRANGE_LIMITED = ColorRange_e::AVS_RANGE_LIMITED;
#endif
