// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include "avs_composite/adapter.h"
#include <cstdint>
#include <vector>

namespace avs_composite {
namespace {
// uint32_t backing provides alignment for all sample types. Scratch belongs to
// this frame call, so instances can be evaluated concurrently.
struct Scratch {
  std::vector<uint32_t> storage;
  cp_plane plane{};
  Scratch(int width, int height, int bytes)
      : storage((size_t(width) * height * bytes + 3) / 4),
        plane{storage.data(), ptrdiff_t(width) * bytes, bytes} {}
};
cp_const_plane Offset(cp_const_plane p, int x, int y) {
  p.data = static_cast<const BYTE*>(p.data) + ptrdiff_t(y) * p.stride + ptrdiff_t(x) * p.step;
  return p;
}
cp_plane Offset(cp_plane p, int x, int y) {
  p.data = static_cast<BYTE*>(p.data) + ptrdiff_t(y) * p.stride + ptrdiff_t(x) * p.step;
  return p;
}
} // namespace
void LayerFrame(PVideoFrame& base, const PVideoFrame& source, const VideoInfo& vi, const VideoInfo& source_vi,
                cp_overlap overlap, LayerOperation operation, bool chroma, bool alpha,
                double opacity, double threshold, int placement, IScriptEnvironment* env) {
  const auto* k = Kernels(env);
  const int bits = vi.BitsPerComponent(), bytes = vi.ComponentSize();
  const cp_format format = Format(bits);
  const cp_rows rows{overlap.width, overlap.height, 0, overlap.height};
  const bool fast = operation == LayerOperation::Fast;
  const bool select = operation == LayerOperation::Lighten || operation == LayerOperation::Darken;
  cp_plane_config config{};
  config.format = format;
  config.opacity = fast ? .5 : Opacity(opacity);
  config.operation = operation == LayerOperation::Subtract ? CP_INVERT_MIX
                   : operation == LayerOperation::Multiply ? CP_PRODUCT
                   : operation == LayerOperation::Lighten ? CP_SELECT_LIGHTER
                   : operation == LayerOperation::Darken ? CP_SELECT_DARKER : CP_MIX;
  config.inversion_sum = bits == 32 ? 1.0 : (1 << bits) - 1;
  config.threshold = threshold;
  config.weight_rule = bits == 32 || fast ? CP_WEIGHT_CONTINUOUS : CP_WEIGHT_CODE;

  if (vi.IsRGB()) {
    const bool packed = !vi.IsPlanar();
    const int count = vi.NumComponents();
    const int planes[] = {PLANAR_B, PLANAR_G, PLANAR_R, PLANAR_A};
    cp_plane dst[4]{};
    cp_const_plane src[4]{};
    for (int c = 0; c < count; ++c) {
      const int p = packed ? DEFAULT_PLANE : planes[c];
      const ptrdiff_t step = packed ? count * bytes : bytes;
      const int offset = packed ? c * bytes : 0;
      dst[c] = Offset(cp_plane{base->GetWritePtr(p) + offset, base->GetPitch(p), step}, overlap.base_x, overlap.base_y);
      src[c] = Offset(cp_const_plane{source->GetReadPtr(p) + offset, source->GetPitch(p), step}, overlap.source_x, overlap.source_y);
    }
    Scratch source_luma(rows.width, (!chroma || select) ? rows.height : 0, bytes);
    Scratch base_luma(rows.width, select ? rows.height : 0, bytes);
    if (!chroma || select)
      Check(k->rgb_luma(format, {src[2], src[1], src[0]}, source_luma.plane, rows, CP_LUMA_FLOOR), env);
    if (select)
      Check(k->rgb_luma(format, {Read(dst[2]), Read(dst[1]), Read(dst[0])}, base_luma.plane, rows, CP_LUMA_FLOOR), env);
    const auto sg = Read(source_luma.plane), bg = Read(base_luma.plane);
    // Packed Fast historically averages alpha; planar Fast leaves it alone.
    const int channels = fast && !packed ? std::min(count, 3) : count;
    for (int c = 0; c < channels; ++c)
      Check(k->process_plane(&config, Read(dst[c]), chroma ? src[c] : sg,
                            alpha && !fast ? &src[3] : nullptr, select ? &bg : nullptr,
                            select ? &sg : nullptr, dst[c], rows), env);
    return;
  }

  const bool packed = vi.IsYUY2();
  const int count = vi.IsY() ? 1 : 3;
  const int planes[] = {PLANAR_Y, PLANAR_U, PLANAR_V};
  const auto base_y = cp_const_plane{base->GetReadPtr(), base->GetPitch(), packed ? 2 : bytes};
  const auto source_y = cp_const_plane{source->GetReadPtr(), source->GetPitch(), packed ? 2 : bytes};
  const auto source_alpha = alpha ? cp_const_plane{source->GetReadPtr(PLANAR_A), source->GetPitch(PLANAR_A), bytes}
                                  : cp_const_plane{};
  // UV guide decisions must see original Y. Alpha remains untouched in YUV.
  for (int index = 0; index < count; ++index) {
    const int c = count == 1 ? 0 : (index + 1) % 3;
    const int ws = c == 0 ? 0 : packed ? 1 : vi.GetPlaneWidthSubsampling(planes[c]);
    const int hs = c == 0 || packed ? 0 : vi.GetPlaneHeightSubsampling(planes[c]);
    const cp_rows part{rows.width >> ws, rows.height >> hs, 0, rows.height >> hs};
    if (!part.width || !part.height) continue;
    const int p = packed ? DEFAULT_PLANE : planes[c];
    const ptrdiff_t step = packed ? c == 0 ? 2 : 4 : bytes;
    const int offset = packed && c != 0 ? c == 1 ? 1 : 3 : 0;
    cp_plane dst = Offset(cp_plane{base->GetWritePtr(p) + offset, base->GetPitch(p), step}, overlap.base_x >> ws, overlap.base_y >> hs);
    cp_const_plane src = Offset(cp_const_plane{source->GetReadPtr(p) + offset, source->GetPitch(p), step}, overlap.source_x >> ws, overlap.source_y >> hs);
    cp_sampling sampling{source_vi.width, source_vi.height, 1 << ws, 1 << hs, placement, overlap.source_x, overlap.source_y};
    Scratch mask(part.width, alpha && !fast && !select ? part.height : 0, bytes);
    if (alpha && !fast && !select)
      Check(k->resample_mask(format, source_alpha, mask.plane, &sampling, part), env);
    Scratch sg(part.width, select ? part.height : 0, bytes), bg(part.width, select ? part.height : 0, bytes);
    if (select) {
      Check(k->resample_mask(format, source_y, sg.plane, &sampling, part), env);
      sampling.source_width = vi.width; sampling.source_height = vi.height;
      sampling.origin_x = overlap.base_x; sampling.origin_y = overlap.base_y;
      Check(k->resample_mask(format, base_y, bg.plane, &sampling, part), env);
    }
    auto current = config;
    Scratch neutral(part.width, c != 0 && !chroma ? part.height : 0, bytes);
    if (c != 0) {
      current.inversion_sum = bits == 32 ? 0.0 : 1 << bits;
      if (!chroma) {
        Check(k->fill(format, neutral.plane, part, bits == 32 ? 0.0 : 1 << (bits - 1)), env);
        src = Read(neutral.plane);
        current.operation = CP_MIX;
        if (operation == LayerOperation::Multiply) current.opacity *= .5;
      } else if (operation == LayerOperation::Multiply) current.operation = CP_MIX;
    }
    const auto m = Read(mask.plane), s = Read(sg.plane), b = Read(bg.plane);
    // The existing YUV Lighten/Darken contract ignores overlay alpha.
    Check(k->process_plane(&current, Read(dst), src, alpha && !fast && !select ? &m : nullptr,
                          select ? &b : nullptr, select ? &s : nullptr, dst, part), env);
  }
}
} // namespace avs_composite
