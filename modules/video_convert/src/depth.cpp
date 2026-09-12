// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include "avs_video_convert/depth.h"
#include "avs_simd/target_policy.h"
#include <vector>
namespace avs_video_convert {
DepthPlans::DepthPlans(int source_bits, int destination_bits, bool destination_full, IScriptEnvironment* env)
    : source_bytes_(source_bits == 32  ? 4
                    : source_bits == 8 ? 1
                                       : 2),
      destination_bytes_(destination_bits == 32  ? 4
                         : destination_bits == 8 ? 1
                                                 : 2) {
  const int64_t target = avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_depth_supported_targets());
  auto create = [&](bool source_full, bool output_full, bool chroma, Plan& plan) {
    const vc_depth_config c{source_bits, destination_bits, source_full, output_full, chroma};
    vc_depth_plan* raw = nullptr;
    const int status = vc_depth_create_for_target(&c, target, &raw);
    if (status != VC_OK)
      env->ThrowError("ConvertBits: cannot create depth plan (%d)", status);
    plan.reset(raw);
  };
  for (int range = 0; range < 2; ++range) {
    create(range != 0, destination_full, false, luma_[range]);
    create(range != 0, destination_full, true, chroma_[range]);
  }
  create(true, true, false, alpha_);
  layout_ = vc_get_layout_functions(avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_layout_supported_targets()));
}
void DepthPlans::Convert(const PVideoFrame& source, PVideoFrame& destination, const VideoInfo& vi, bool source_full,
                         IScriptEnvironment* env) const {
  auto execute = [&](const Plan& plan, vc_const_plane src, vc_plane dst, int width, int height) {
    const int status = vc_depth_execute(plan.get(), src, dst, {width, height, 0, height});
    if (status != VC_OK)
      env->ThrowError("ConvertBits: invalid frame layout (%d)", status);
  };
  if (vi.IsPlanar()) {
    const int yuv[] = {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}, rgb[] = {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A};
    const int* planes = vi.IsRGB() ? rgb : yuv;
    for (int p = 0; p < vi.NumComponents(); ++p) {
      const int plane = planes[p];
      const auto& plan = plane == PLANAR_A                          ? alpha_
                         : (plane == PLANAR_U || plane == PLANAR_V) ? chroma_[source_full]
                                                                    : luma_[source_full];
      execute(plan, {source->GetReadPtr(plane), source->GetPitch(plane)},
              {destination->GetWritePtr(plane), destination->GetPitch(plane)},
              source->GetRowSize(plane) / source_bytes_, source->GetHeight(plane));
    }
    return;
  }
  if (vi.NumComponents() == 3) {
    // Packed RGB without alpha is one contiguous stream of equivalent channels.
    execute(luma_[source_full], {source->GetReadPtr(), source->GetPitch()},
            {destination->GetWritePtr(), destination->GetPitch()}, source->GetRowSize() / source_bytes_, vi.height);
    return;
  }
  // Packed alpha has a full-range numeric contract independent of RGB range.
  // Row scratch preserves bottom-up storage and avoids full-frame intermediates.
  const size_t sw = (size_t(vi.width) * source_bytes_ + 1) / 2, dw = (size_t(vi.width) * destination_bytes_ + 1) / 2;
  std::vector<uint16_t> scratch(4 * (sw + dw));
  const int ss = source_bytes_ == 1 ? VC_U8 : VC_U16, ds = destination_bytes_ == 1 ? VC_U8 : VC_U16;
  const vc_rows rows{vi.width, 1, 0, 1};
  for (int y = 0; y < vi.height; ++y) {
    vc_plane input[4], output[4];
    for (int c = 0; c < 4; ++c) {
      input[c] = {scratch.data() + c * sw, ptrdiff_t(vi.width * source_bytes_)};
      output[c] = {scratch.data() + 4 * sw + c * dw, ptrdiff_t(vi.width * destination_bytes_)};
    }
    int status = layout_->unpack_bgr({source->GetReadPtr() + ptrdiff_t(y) * source->GetPitch(), source->GetPitch()},
                                     {input[2], input[1], input[0], input[3]}, ss, 4, 0, rows);
    if (status != VC_OK)
      env->ThrowError("ConvertBits: invalid packed source (%d)", status);
    for (int c = 0; c < 4; ++c)
      execute(c == 3 ? alpha_ : luma_[source_full], {input[c].data, input[c].stride}, output[c], vi.width, 1);
    status = layout_->pack_bgr(
        {{output[2].data, output[2].stride},
         {output[1].data, output[1].stride},
         {output[0].data, output[0].stride},
         {output[3].data, output[3].stride}},
        {destination->GetWritePtr() + ptrdiff_t(y) * destination->GetPitch(), destination->GetPitch()}, ds, 4, 0, rows);
    if (status != VC_OK)
      env->ThrowError("ConvertBits: invalid packed destination (%d)", status);
  }
}
void DepthPlans::ConvertAlpha(const PVideoFrame& source, PVideoFrame& destination, IScriptEnvironment* env) const {
  const int status = vc_depth_execute(
      alpha_.get(), {source->GetReadPtr(PLANAR_A), source->GetPitch(PLANAR_A)},
      {destination->GetWritePtr(PLANAR_A), destination->GetPitch(PLANAR_A)},
      {source->GetRowSize(PLANAR_A) / source_bytes_, source->GetHeight(PLANAR_A), 0, source->GetHeight(PLANAR_A)});
  if (status != VC_OK)
    env->ThrowError("ConvertBits: invalid alpha plane (%d)", status);
}
} // namespace avs_video_convert
