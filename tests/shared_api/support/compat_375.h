#pragma once

// These shared-library cases use the installed public API only. The host test
// helper also serves legacy internal tests, whose private conversion constants
// are neither needed nor exposed by this standalone consumer.
#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#endif
