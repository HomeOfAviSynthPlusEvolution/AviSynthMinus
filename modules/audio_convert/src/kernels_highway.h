#pragma once

#include "kernels.h"
#include <cstdint>

namespace avs_audio_convert {

// Resolves any of the twenty conversions between U8, S16, S24, S32 and F32.
// SIMD targets use Highway; the fallback selects ordinary C.
//
// src_format and dst_format are AviSynth sample type bitmasks (e.g. SAMPLE_INT8,
// SAMPLE_INT16, SAMPLE_INT24, SAMPLE_INT32, SAMPLE_FLOAT from avisynth.h).
//
// If (src_format, dst_format) is not supported, returns nullptr.
// If target is TARGET_C_FALLBACK, returns the corresponding ordinary C function (or nullptr for S24 <-> F32).
// Otherwise, returns the compiled Highway SIMD kernel for the chosen target.
convert_proc ResolveHighwayAudioConvert(int src_format, int dst_format, int avs_cpu_flags);

// Resolves for an explicit Highway target (e.g. HWY_AVX2, HWY_SSE2, or avs_simd::TARGET_C_FALLBACK).
convert_proc ResolveHighwayAudioConvertForTarget(int src_format, int dst_format, int64_t target);

// Returns the Highway target that ChooseTarget selects for this translation unit
// under the given AVS CPU flags.
int64_t GetHighwayAudioConvertChosenTarget(int avs_cpu_flags);

// Returns the bitmask of targets compiled into this translation unit (HWY_TARGETS).
int64_t GetHighwayAudioConvertCompiledTargets();

// Checks whether a route is supported by this Highway module.
bool IsHighwayAudioConvertSupportedRoute(int src_format, int dst_format);

}  // namespace avs_audio_convert
