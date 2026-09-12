// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include "avs_video_convert/matrix.h"
#include "avs_simd/target_policy.h"
#include <vector>
#include <cstring>
namespace avs_video_convert {
MatrixPlan::MatrixPlan(double kr, double kb, int depth, int precision, bool source_full, bool destination_full,
                       bool rgb_to_yuv, IScriptEnvironment* env, bool luma_only)
    : rgb_to_yuv_(rgb_to_yuv), luma_only_(luma_only), bytes_(depth == 8    ? 1
                                                             : depth == 32 ? 4
                                                                           : 2) {
  const vc_matrix_config config{kr,
                                kb,
                                depth,
                                precision,
                                source_full,
                                destination_full,
                                luma_only    ? VC_RGB_TO_Y
                                : rgb_to_yuv ? VC_RGB_TO_YUV
                                             : depth == 32 ? VC_YUV_TO_RGB_UNCLIPPED : VC_YUV_TO_RGB};
  const int64_t target = avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_matrix_supported_targets());
  vc_matrix_plan* raw = nullptr;
  const int status = vc_matrix_create_for_target(&config, target, &raw);
  if (status != VC_OK)
    env->ThrowError("Matrix conversion: cannot create numeric plan (%d)", status);
  plan_.reset(raw);
  layout_ = vc_get_layout_functions(avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_layout_supported_targets()));
}
void MatrixPlan::Convert(const PVideoFrame& source, PVideoFrame& destination, int width, int height,
                         IScriptEnvironment* env) const {
  const auto input = [&](int plane) -> vc_const_plane {
    return {source->GetReadPtr(plane), source->GetPitch(plane)};
  };
  const auto output = [&](int plane) -> vc_plane {
    return {destination->GetWritePtr(plane), destination->GetPitch(plane)};
  };
  const vc_rows rows{width, height, 0, height};
  const int status =
      luma_only_    ? vc_matrix_rgb_to_y(plan_.get(), {input(PLANAR_R), input(PLANAR_G), input(PLANAR_B), {}},
                                         output(PLANAR_Y), rows)
      : rgb_to_yuv_ ? vc_matrix_rgb_to_yuv(plan_.get(), {input(PLANAR_R), input(PLANAR_G), input(PLANAR_B), {}},
                                           {output(PLANAR_Y), output(PLANAR_U), output(PLANAR_V)}, rows)
                    : vc_matrix_yuv_to_rgb(plan_.get(), {input(PLANAR_Y), input(PLANAR_U), input(PLANAR_V)},
                                           {output(PLANAR_R), output(PLANAR_G), output(PLANAR_B), {}}, rows);
  if (status != VC_OK)
    env->ThrowError("Matrix conversion: invalid frame layout (%d)", status);
}
void MatrixPlan::ConvertPacked(const PVideoFrame& source, PVideoFrame& destination, int width, int height,
                               int components, bool alpha, IScriptEnvironment* env) const {
  // A local three-plane row buffer keeps frames concurrent and avoids a full
  // planar intermediate. uint16_t storage supplies natural alignment for U16.
  const size_t row_bytes = size_t(width) * bytes_;
  const size_t row_words = (row_bytes + 1) / 2;
  std::vector<uint16_t> scratch(3 * row_words);
  const vc_rows rows{width, 1, 0, 1};
  const int storage = bytes_ == 1 ? VC_U8 : VC_U16;
  const uint32_t opaque = bytes_ == 1 ? 255 : 65535;
  for (int y = 0; y < height; ++y) {
    const auto input = [&](int plane) -> vc_const_plane {
      return {source->GetReadPtr(plane) + ptrdiff_t(y) * source->GetPitch(plane), source->GetPitch(plane)};
    };
    const auto output = [&](int plane) -> vc_plane {
      return {destination->GetWritePtr(plane) + ptrdiff_t(y) * destination->GetPitch(plane),
              destination->GetPitch(plane)};
    };
    const vc_plane b{scratch.data(), ptrdiff_t(row_bytes)}, g{scratch.data() + row_words, ptrdiff_t(row_bytes)},
        r{scratch.data() + 2 * row_words, ptrdiff_t(row_bytes)};
    int status;
    if (rgb_to_yuv_) {
      const vc_const_plane packed{source->GetReadPtr() + ptrdiff_t(height - 1 - y) * source->GetPitch(),
                                  source->GetPitch()};
      status = layout_->unpack_bgr(packed, {r, g, b, alpha ? output(PLANAR_A) : vc_plane{}}, storage, components,
                                   opaque, rows);
      if (status == VC_OK)
        status =
            luma_only_
                ? vc_matrix_rgb_to_y(plan_.get(), {{r.data, r.stride}, {g.data, g.stride}, {b.data, b.stride}, {}},
                                     output(PLANAR_Y), rows)
                : vc_matrix_rgb_to_yuv(plan_.get(), {{r.data, r.stride}, {g.data, g.stride}, {b.data, b.stride}, {}},
                                       {output(PLANAR_Y), output(PLANAR_U), output(PLANAR_V)}, rows);
    } else {
      status =
          vc_matrix_yuv_to_rgb(plan_.get(), {input(PLANAR_Y), input(PLANAR_U), input(PLANAR_V)}, {r, g, b, {}}, rows);
      const vc_plane packed{destination->GetWritePtr() + ptrdiff_t(height - 1 - y) * destination->GetPitch(),
                            destination->GetPitch()};
      if (status == VC_OK)
        status = layout_->pack_bgr(
            {{r.data, r.stride}, {g.data, g.stride}, {b.data, b.stride}, alpha ? input(PLANAR_A) : vc_const_plane{}},
            packed, storage, components, opaque, rows);
    }
    if (status != VC_OK)
      env->ThrowError("Matrix conversion: invalid packed frame layout (%d)", status);
  }
}
void MatrixPlan::ApplyGreyscale(PVideoFrame& frame, const VideoInfo& vi, IScriptEnvironment* env) const {
  const bool packed = !vi.IsPlanar();
  const int components = vi.NumComponents();
  const size_t row_bytes = size_t(vi.width) * bytes_, row_words = (row_bytes + 3) / 4;
  // Separate luma storage avoids aliasing the matrix source planes. Packed RGB
  // also needs color/alpha rows until all input channels have been consumed.
  std::vector<uint32_t> scratch((packed ? 5 : 1) * row_words);
  const vc_plane luma{scratch.data(), ptrdiff_t(row_bytes)};
  const vc_rows rows{vi.width, 1, 0, 1};
  const int storage = bytes_ == 1 ? VC_U8 : VC_U16;
  for (int y = 0; y < vi.height; ++y) {
    int status;
    if (packed) {
      const vc_plane b{scratch.data() + row_words, ptrdiff_t(row_bytes)},
          g{scratch.data() + 2 * row_words, ptrdiff_t(row_bytes)},
          r{scratch.data() + 3 * row_words, ptrdiff_t(row_bytes)},
          a{scratch.data() + 4 * row_words, ptrdiff_t(row_bytes)};
      const vc_plane row{frame->GetWritePtr() + ptrdiff_t(y) * frame->GetPitch(), frame->GetPitch()};
      status = layout_->unpack_bgr({row.data, row.stride}, {r, g, b, a}, storage, components, 0, rows);
      if (status == VC_OK)
        status = vc_matrix_rgb_to_y(plan_.get(), {{r.data, r.stride}, {g.data, g.stride}, {b.data, b.stride}, {}}, luma,
                                    rows);
      if (status == VC_OK)
        status = layout_->pack_bgr(
            {{luma.data, luma.stride}, {luma.data, luma.stride}, {luma.data, luma.stride}, {a.data, a.stride}}, row,
            storage, components, 0, rows);
    } else {
      const auto input = [&](int plane) -> vc_const_plane {
        return {frame->GetReadPtr(plane) + ptrdiff_t(y) * frame->GetPitch(plane), frame->GetPitch(plane)};
      };
      status = vc_matrix_rgb_to_y(plan_.get(), {input(PLANAR_R), input(PLANAR_G), input(PLANAR_B), {}}, luma, rows);
      if (status == VC_OK)
        for (int plane : {PLANAR_R, PLANAR_G, PLANAR_B})
          std::memcpy(frame->GetWritePtr(plane) + ptrdiff_t(y) * frame->GetPitch(plane), luma.data, row_bytes);
    }
    if (status != VC_OK)
      env->ThrowError("Greyscale: invalid RGB row layout (%d)", status);
  }
}
} // namespace avs_video_convert
