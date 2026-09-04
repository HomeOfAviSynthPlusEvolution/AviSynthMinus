// This translation unit is selected only by ENABLE_EXPRFILTER_DIAGNOSTIC_STUB.
// It exists to measure the build-time contribution of exprfilter.cpp while
// retaining the Expr registration symbol required to link AviSynth.dll.

#include <string>
#include <vector>

#include <avisynth.h>

#include "../../core/internal.h"
#include "exprfilter.h"

extern const AVSFunction Exprfilter_filters[] = {
  { "Expr", BUILTIN_FUNC_PREFIX,
    "c+s+[format]s[optAvx2]b[optSingleMode]b[optSSE2]b[scale_inputs]s[clamp_float]b[clamp_float_UV]b[lut]i[optVectorC]b",
    Exprfilter::Create },
  { 0 }
};

AVSValue __cdecl Exprfilter::Create(AVSValue, void*, IScriptEnvironment* env) {
  env->ThrowError("Expr is unavailable in this compiler-timing diagnostic build.");
  return AVSValue();
}
