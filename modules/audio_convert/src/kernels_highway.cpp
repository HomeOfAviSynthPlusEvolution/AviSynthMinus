#include "kernels_highway.h"
#include "kernels.h"
#include <avisynth.h>
#include "avs_simd/highway_config.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "modules/audio_convert/src/kernels_highway.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace audio_convert {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// Scalar fallback is supplied directly by kTableC, not by Highway wrappers.
#if HWY_TARGET != HWY_SCALAR

// S32 -> S16: high 16 bits of 32-bit signed integer (in >> 16)
void convert32To16_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  auto in = static_cast<const int32_t*>(inbuf);
  auto out = static_cast<int16_t*>(outbuf);

  const hn::ScalableTag<int32_t> d32;
  const hn::ScalableTag<int16_t> d16;
  const size_t N16 = hn::Lanes(d16);
  const size_t N32 = hn::Lanes(d32);

  const size_t vector_end = static_cast<size_t>(count) - static_cast<size_t>(count) % N16;
  size_t i = 0;
  for (; i < vector_end; i += N16) {
    const auto v0 = hn::LoadU(d32, in + i);
    const auto v1 = hn::LoadU(d32, in + i + N32);
    const auto s0 = hn::ShiftRight<16>(v0);
    const auto s1 = hn::ShiftRight<16>(v1);
    const auto res = hn::OrderedDemote2To(d16, s0, s1);
    hn::StoreU(res, d16, out + i);
  }
  for (; i < static_cast<size_t>(count); ++i) {
    out[i] = static_cast<int16_t>(in[i] >> 16);
  }
}

// S16 -> S32: widen to upper 16 bits of 32-bit integer, low 16 bits 0
void convert16To32_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  auto in_u16 = static_cast<const uint16_t*>(inbuf);
  auto out = static_cast<int32_t*>(outbuf);

  const hn::ScalableTag<uint16_t> du16;
  const hn::ScalableTag<uint32_t> du32;
  const hn::ScalableTag<int32_t> d32;
  const hn::Rebind<uint16_t, decltype(du32)> dh16;
  const size_t N16 = hn::Lanes(du16);
  const size_t N32 = hn::Lanes(du32);

  const size_t vector_end = static_cast<size_t>(count) - static_cast<size_t>(count) % N16;
  size_t i = 0;
  for (; i < vector_end; i += N16) {
    // Load each half at its input width so widening does not first require
    // extracting halves of a full-width vector.
    const auto lo32 = hn::ShiftLeft<16>(hn::PromoteTo(du32, hn::LoadU(dh16, in_u16 + i)));
    const auto hi32 = hn::ShiftLeft<16>(hn::PromoteTo(du32, hn::LoadU(dh16, in_u16 + i + N32)));
    hn::StoreU(hn::BitCast(d32, lo32), d32, out + i);
    hn::StoreU(hn::BitCast(d32, hi32), d32, out + i + N32);
  }
  for (; i < static_cast<size_t>(count); ++i) {
    out[i] = static_cast<int32_t>(static_cast<uint32_t>(in_u16[i]) << 16);
  }
}

// S32 -> U8: (in >> 24) + 128 == (uint32(in) >> 24) ^ 0x80
void convert32To8_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  auto in = static_cast<const int32_t*>(inbuf);
  auto out = static_cast<uint8_t*>(outbuf);

  const hn::ScalableTag<int32_t> d32;
  const hn::ScalableTag<int16_t> d16;
  const hn::ScalableTag<int8_t> di8;
  const hn::ScalableTag<uint8_t> du8;
  const size_t N32 = hn::Lanes(d32);
  const size_t N8 = hn::Lanes(du8);

  const size_t vector_end = static_cast<size_t>(count) - static_cast<size_t>(count) % N8;
  size_t i = 0;
  for (; i < vector_end; i += N8) {
    const auto v0 = hn::LoadU(d32, in + i);
    const auto v1 = hn::LoadU(d32, in + i + N32);
    const auto v2 = hn::LoadU(d32, in + i + 2 * N32);
    const auto v3 = hn::LoadU(d32, in + i + 3 * N32);

    const auto s0 = hn::ShiftRight<16>(v0);
    const auto s1 = hn::ShiftRight<16>(v1);
    const auto s2 = hn::ShiftRight<16>(v2);
    const auto s3 = hn::ShiftRight<16>(v3);

    const auto i16_0 = hn::OrderedDemote2To(d16, s0, s1);
    const auto i16_1 = hn::OrderedDemote2To(d16, s2, s3);

    const auto t0 = hn::ShiftRight<8>(i16_0);
    const auto t1 = hn::ShiftRight<8>(i16_1);

    const auto i8 = hn::OrderedDemote2To(di8, t0, t1);
    const auto u8 = hn::Add(hn::BitCast(du8, i8), hn::Set(du8, 0x80));
    hn::StoreU(u8, du8, out + i);
  }
  for (; i < static_cast<size_t>(count); ++i) {
    out[i] = static_cast<uint8_t>((static_cast<uint32_t>(in[i]) >> 24) ^ 0x80);
  }
}

// U8 -> S32: (in ^ 0x80) << 24
void convert8To32_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  auto in = static_cast<const uint8_t*>(inbuf);
  auto out = static_cast<int32_t*>(outbuf);

  const hn::ScalableTag<uint32_t> du32;
  const hn::ScalableTag<int32_t> d32;
  const hn::Rebind<uint8_t, decltype(du32)> du8_for_32;
  const size_t N = hn::Lanes(du32);

  const size_t vector_end = static_cast<size_t>(count) - static_cast<size_t>(count) % N;
  size_t i = 0;
  for (; i < vector_end; i += N) {
    const auto v8 = hn::LoadU(du8_for_32, in + i);
    const auto v8_s = hn::Add(v8, hn::Set(du8_for_32, 0x80));
    const auto v32 = hn::ShiftLeft<24>(hn::PromoteTo(du32, v8_s));
    hn::StoreU(hn::BitCast(d32, v32), d32, out + i);
  }
  for (; i < static_cast<size_t>(count); ++i) {
    out[i] = static_cast<int32_t>(static_cast<uint32_t>(in[i] ^ 0x80) << 24);
  }
}

// S16 -> U8: (in >> 8) + 128 == (uint16(in) >> 8) ^ 0x80
void convert16To8_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  auto in = static_cast<const int16_t*>(inbuf);
  auto out = static_cast<uint8_t*>(outbuf);

  const hn::ScalableTag<int16_t> d16;
  const hn::ScalableTag<int8_t> di8;
  const hn::ScalableTag<uint8_t> du8;
  const size_t N16 = hn::Lanes(d16);
  const size_t N8 = hn::Lanes(du8);

  const size_t vector_end = static_cast<size_t>(count) - static_cast<size_t>(count) % N8;
  size_t i = 0;
  for (; i < vector_end; i += N8) {
    const auto v0 = hn::LoadU(d16, in + i);
    const auto v1 = hn::LoadU(d16, in + i + N16);
    const auto s0 = hn::ShiftRight<8>(v0);
    const auto s1 = hn::ShiftRight<8>(v1);
    const auto i8 = hn::OrderedDemote2To(di8, s0, s1);
    const auto u8 = hn::Add(hn::BitCast(du8, i8), hn::Set(du8, 0x80));
    hn::StoreU(u8, du8, out + i);
  }
  for (; i < static_cast<size_t>(count); ++i) {
    out[i] = static_cast<uint8_t>((static_cast<uint16_t>(in[i]) >> 8) ^ 0x80);
  }
}

// U8 -> S16: (in ^ 0x80) << 8
void convert8To16_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  auto in = static_cast<const uint8_t*>(inbuf);
  auto out = static_cast<int16_t*>(outbuf);

  const hn::ScalableTag<uint8_t> du8;
  const hn::ScalableTag<uint16_t> du16;
  const hn::ScalableTag<int16_t> d16;
  const size_t N8 = hn::Lanes(du8);
  const size_t N16 = hn::Lanes(du16);

  const size_t vector_end = static_cast<size_t>(count) - static_cast<size_t>(count) % N8;
  size_t i = 0;
  for (; i < vector_end; i += N8) {
    const auto v8 = hn::LoadU(du8, in + i);
    const auto v8_s = hn::Add(v8, hn::Set(du8, 0x80));
    const auto v16_0 = hn::ShiftLeft<8>(hn::PromoteLowerTo(du16, v8_s));
    const auto v16_1 = hn::ShiftLeft<8>(hn::PromoteUpperTo(du16, v8_s));
    hn::StoreU(hn::BitCast(d16, v16_0), d16, out + i);
    hn::StoreU(hn::BitCast(d16, v16_1), d16, out + i + N16);
  }
  for (; i < static_cast<size_t>(count); ++i) {
    out[i] = static_cast<int16_t>(static_cast<uint16_t>(in[i] ^ 0x80) << 8);
  }
}

#endif  // HWY_TARGET != HWY_SCALAR

}  // namespace HWY_NAMESPACE
}  // namespace audio_convert
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
#include "avs_simd/target_policy.h"

namespace audio_convert {

struct RouteTable {
  convert_proc s32_to_s16;
  convert_proc s16_to_s32;
  convert_proc s32_to_u8;
  convert_proc u8_to_s32;
  convert_proc s16_to_u8;
  convert_proc u8_to_s16;
};

static const RouteTable kTableC = {
  convert32To16,
  convert16To32,
  convert32To8,
  convert8To32,
  convert16To8,
  convert8To16,
};

#define MAKE_ROUTE_TABLE(TARGET_MACRO) \
  { \
    TARGET_MACRO(convert32To16_HWY), \
    TARGET_MACRO(convert16To32_HWY), \
    TARGET_MACRO(convert32To8_HWY),  \
    TARGET_MACRO(convert8To32_HWY),  \
    TARGET_MACRO(convert16To8_HWY),  \
    TARGET_MACRO(convert8To16_HWY)   \
  }

static const RouteTable* GetRouteTableForTarget(int64_t target) {
  switch (target) {
#if HWY_ARCH_X86
#if HWY_TARGETS & HWY_AVX10_2
    case HWY_AVX10_2: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_AVX10_2);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_AVX3_SPR
    case HWY_AVX3_SPR: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_AVX3_SPR);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_AVX3_ZEN4
    case HWY_AVX3_ZEN4: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_AVX3_ZEN4);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_AVX3_DL
    case HWY_AVX3_DL: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_AVX3_DL);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_AVX3
    case HWY_AVX3: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_AVX3);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_AVX2
    case HWY_AVX2: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_AVX2);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_SSE4
    case HWY_SSE4: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_SSE4);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_SSSE3
    case HWY_SSSE3: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_SSSE3);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_SSE2
    case HWY_SSE2: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_SSE2);
      return &table;
    }
#endif
#endif  // HWY_ARCH_X86

#if HWY_ARCH_ARM
#if HWY_TARGETS & HWY_SVE2_128
    case HWY_SVE2_128: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_SVE2_128);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_SVE_256
    case HWY_SVE_256: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_SVE_256);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_SVE2
    case HWY_SVE2: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_SVE2);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_SVE
    case HWY_SVE: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_SVE);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_NEON_BF16
    case HWY_NEON_BF16: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_NEON_BF16);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_NEON
    case HWY_NEON: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_NEON);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_NEON_WITHOUT_AES
    case HWY_NEON_WITHOUT_AES: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_NEON_WITHOUT_AES);
      return &table;
    }
#endif
#endif  // HWY_ARCH_ARM

#if HWY_TARGETS & HWY_RVV
    case HWY_RVV: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_RVV);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_PPC8
    case HWY_PPC8: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_PPC8);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_PPC9
    case HWY_PPC9: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_PPC9);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_PPC10
    case HWY_PPC10: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_PPC10);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_Z14
    case HWY_Z14: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_Z14);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_Z15
    case HWY_Z15: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_Z15);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_LSX
    case HWY_LSX: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_LSX);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_LASX
    case HWY_LASX: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_LASX);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_WASM
    case HWY_WASM: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_WASM);
      return &table;
    }
#endif
#if HWY_TARGETS & HWY_WASM_EMU256
    case HWY_WASM_EMU256: {
      static const RouteTable table = MAKE_ROUTE_TABLE(HWY_CHOOSE_WASM_EMU256);
      return &table;
    }
#endif

    default:
      break;
  }
  return &kTableC;
}

#undef MAKE_ROUTE_TABLE

}  // namespace audio_convert

convert_proc ResolveHighwayAudioConvertForTarget(int src_format, int dst_format, int64_t target) {
  const audio_convert::RouteTable* table =
      (target == avs_simd::TARGET_C_FALLBACK)
          ? &audio_convert::kTableC
          : audio_convert::GetRouteTableForTarget(target);
  if (!table) table = &audio_convert::kTableC;

  const int route = (src_format << 16) | dst_format;
  switch (route) {
    case (SAMPLE_INT32 << 16) | SAMPLE_INT16: return table->s32_to_s16;
    case (SAMPLE_INT16 << 16) | SAMPLE_INT32: return table->s16_to_s32;
    case (SAMPLE_INT32 << 16) | SAMPLE_INT8:  return table->s32_to_u8;
    case (SAMPLE_INT8  << 16) | SAMPLE_INT32: return table->u8_to_s32;
    case (SAMPLE_INT16 << 16) | SAMPLE_INT8:  return table->s16_to_u8;
    case (SAMPLE_INT8  << 16) | SAMPLE_INT16: return table->u8_to_s16;
    default: return nullptr;
  }
}

convert_proc ResolveHighwayAudioConvert(int src_format, int dst_format, int avs_cpu_flags) {
  if (!IsHighwayAudioConvertSupportedRoute(src_format, dst_format)) {
    return nullptr;
  }
  const int64_t chosen = avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
  return ResolveHighwayAudioConvertForTarget(src_format, dst_format, chosen);
}

int64_t GetHighwayAudioConvertChosenTarget(int avs_cpu_flags) {
  return avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
}

int64_t GetHighwayAudioConvertCompiledTargets() {
  return HWY_TARGETS;
}

bool IsHighwayAudioConvertSupportedRoute(int src_format, int dst_format) {
  const int route = (src_format << 16) | dst_format;
  switch (route) {
    case (SAMPLE_INT32 << 16) | SAMPLE_INT16:
    case (SAMPLE_INT16 << 16) | SAMPLE_INT32:
    case (SAMPLE_INT32 << 16) | SAMPLE_INT8:
    case (SAMPLE_INT8  << 16) | SAMPLE_INT32:
    case (SAMPLE_INT16 << 16) | SAMPLE_INT8:
    case (SAMPLE_INT8  << 16) | SAMPLE_INT16:
      return true;
    default:
      return false;
  }
}

#endif  // HWY_ONCE
