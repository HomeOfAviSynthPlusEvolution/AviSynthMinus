// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#pragma once
#include <avisynth.h>
#include <composite/dispatch.h>
#include <avs_simd/target_policy.h>
#include <algorithm>
#include <cmath>
namespace avs_composite {
inline cp_format Format(int bits) { return {bits == 8 ? CP_U8 : bits == 32 ? CP_F32 : CP_U16, bits}; }
inline cp_const_plane Read(cp_plane p) { return {p.data, p.stride, p.step}; }
inline void Check(int status, IScriptEnvironment* env) {
  if (status != CP_OK) env->ThrowError("Composite: invalid kernel arguments (%d)", status);
}
inline const cp_kernels* Kernels(IScriptEnvironment* env) {
  const auto* kernels = cp_get_kernels(avs_simd::ChooseTarget(env->GetCPUFlagsEx(), cp_compiled_targets()));
  if (!kernels) env->ThrowError("Composite: CPU target is unavailable");
  return kernels;
}
inline double Opacity(double value) { return std::isnan(value) ? 0.0 : std::clamp(value, 0.0, 1.0); }
inline void Mix(cp_plane base, cp_const_plane source, cp_rows rows, int bits, double opacity,
                IScriptEnvironment* env) {
  if (rows.width == 0 || rows.height == 0) return;
  cp_plane_config config{};
  config.format = Format(bits);
  config.operation = CP_MIX;
  config.opacity = Opacity(opacity);
  Check(Kernels(env)->process_plane(&config, Read(base), source, nullptr, nullptr, nullptr, base, rows), env);
}
inline void MergePlane(BYTE* base, const BYTE* source, int base_pitch, int source_pitch,
                       int row_bytes, int height, int bits, double weight, IScriptEnvironment* env) {
  const int bytes = bits == 8 ? 1 : bits == 32 ? 4 : 2;
  Mix({base, base_pitch, bytes}, {source, source_pitch, bytes},
      {row_bytes / bytes, height, 0, height}, bits, weight, env);
}
enum class LayerOperation { Add, Subtract, Multiply, Fast, Lighten, Darken };
void LayerFrame(PVideoFrame& base, const PVideoFrame& source, const VideoInfo& vi, const VideoInfo& source_vi,
                cp_overlap overlap, LayerOperation operation, bool chroma, bool alpha,
                double opacity, double threshold, int placement, IScriptEnvironment* env);
} // namespace avs_composite
