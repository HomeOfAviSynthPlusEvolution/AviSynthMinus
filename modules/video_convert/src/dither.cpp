// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include "avs_video_convert/dither.h"
#include "avs_simd/target_policy.h"
namespace avs_video_convert {
DitherPlans::DitherPlans(int source_bits, int destination_bits, int quantization_bits, int mode, bool destination_full,
                         IScriptEnvironment* env)
    : source_bytes_(source_bits == 8 ? 1 : 2), ordered_mode_(mode == 0) {
  const int64_t target = avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_ordered_supported_targets());
  for (int sf = 0; sf < 2; ++sf)
    for (int chroma = 0; chroma < 2; ++chroma) {
      const int i = sf * 2 + chroma;
      configs_[i] = {{source_bits, destination_bits, sf, destination_full, chroma}, quantization_bits};
      if (ordered_mode_) {
        const vc_ordered_config c{configs_[i].depth, quantization_bits};
        vc_ordered_plan* raw = nullptr;
        const int status = vc_ordered_create_for_target(&c, target, &raw);
        if (status != VC_OK)
          env->ThrowError("ConvertBits: cannot create ordered dither plan (%d)", status);
        ordered_[i].reset(raw);
      }
    }
}
void DitherPlans::ConvertPlane(const PVideoFrame& source, PVideoFrame& destination, int plane, bool source_full,
                               IScriptEnvironment* env) const {
  const int i = int(source_full) * 2 + int(plane == PLANAR_U || plane == PLANAR_V);
  const vc_const_plane s{source->GetReadPtr(plane), source->GetPitch(plane)};
  const vc_plane d{destination->GetWritePtr(plane), destination->GetPitch(plane)};
  const vc_rows rows{source->GetRowSize(plane) / source_bytes_, source->GetHeight(plane), 0, source->GetHeight(plane)};
  int status;
  if (ordered_mode_)
    status = vc_ordered_execute(ordered_[i].get(), s, d, rows);
  else {
    vc_floyd_context* raw = nullptr;
    status = vc_floyd_create(&configs_[i], rows.width, rows.height, &raw);
    if (status != VC_OK)
      env->ThrowError("ConvertBits: cannot allocate diffusion context (%d)", status);
    const std::unique_ptr<vc_floyd_context, decltype(&vc_floyd_destroy)> context(raw, vc_floyd_destroy);
    status = vc_floyd_execute(raw, s, d, rows);
  }
  if (status != VC_OK)
    env->ThrowError("ConvertBits: invalid dither plane (%d)", status);
}
} // namespace avs_video_convert
