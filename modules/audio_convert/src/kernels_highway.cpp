#include "kernels_highway.h"
#include "kernels.h"
#include <avisynth.h>
#include "avs_simd/highway_config.h"

#include <cmath>
#include <array>
#include <utility>
#include <type_traits>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "modules/audio_convert/src/kernels_highway.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace avs_audio_convert {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// Scalar fallback is supplied directly by kTableC, not by Highway wrappers.
#if HWY_TARGET != HWY_SCALAR

// Quantize only for packing S24, not as a general F32 -> S32 converter.
template<class DF>
HWY_INLINE auto QuantizeForPacked24(DF df, const hn::Vec<DF>& input) {
  const hn::Rebind<int32_t, DF> di;
  const auto scaled = hn::Mul(hn::IfThenElseZero(hn::Eq(input, input), input),
                             hn::Set(df, 2147483648.0f));
  // The discarded low byte makes INT32_MAX and 2147483520 equivalent here.
  return hn::ConvertInRangeTo(di, hn::Max(hn::Set(df, -2147483648.0f),
                                       hn::Min(hn::Set(df, 2147483520.0f), scaled)));
}

template<class T>
HWY_INLINE T UnpackS24Sample(const uint8_t* src) {
  const uint32_t bits = (uint32_t(src[0]) << 8) | (uint32_t(src[1]) << 16) |
                        (uint32_t(src[2]) << 24);
  if constexpr (std::is_same_v<T, uint8_t>) return uint8_t(src[2] ^ 0x80);
  else if constexpr (std::is_same_v<T, int16_t>) return static_cast<int16_t>(bits >> 16);
  else if constexpr (std::is_same_v<T, int32_t>) return static_cast<int32_t>(bits);
  else return static_cast<int32_t>(bits) * (1.0f / 2147483648.0f);
}

template<class T>
HWY_INLINE void PackS24Sample(T value, uint8_t* dst) {
  uint32_t bits;
  if constexpr (std::is_same_v<T, uint8_t>) bits = uint32_t(value ^ 0x80) << 24;
  else if constexpr (std::is_same_v<T, int16_t>) bits = uint32_t(uint16_t(value)) << 16;
  else if constexpr (std::is_same_v<T, int32_t>) bits = static_cast<uint32_t>(value);
  else {
    const float scaled = value * 2147483648.0f;
    int32_t s;
    if (std::isnan(scaled)) s = 0;
    else if (scaled >= 2147483648.0f) s = INT32_MAX;
    else if (scaled <= -2147483648.0f) s = INT32_MIN;
    else s = static_cast<int32_t>(scaled);
    bits = static_cast<uint32_t>(s);
  }
  dst[0] = static_cast<uint8_t>(bits >> 8);
  dst[1] = static_cast<uint8_t>(bits >> 16);
  dst[2] = static_cast<uint8_t>(bits >> 24);
}


// SVE/RVV vectors are sizeless and cannot be stored in C++ arrays. Use native
// interleaved loads/stores there; fixed-width targets use hoisted byte masks.
#if HWY_HAVE_SCALABLE || HWY_TARGET_IS_SVE
template<class T>
HWY_INLINE void UnpackS24(const uint8_t* in, T* out, size_t count) {
  const hn::ScalableTag<T> d;
  const hn::Rebind<uint8_t, decltype(d)> d8;
  const size_t n = hn::Lanes(d);
  const size_t end = count - count % n;
  size_t i = 0;
  for (; i < end; i += n) {
    auto low = hn::Zero(d8), middle = low, high = low;
    hn::LoadInterleaved3(d8, in + i * 3, low, middle, high);
    if constexpr (std::is_same_v<T, uint8_t>) {
      hn::StoreU(hn::Xor(high, hn::Set(d, 0x80)), d, out + i);
    } else if constexpr (std::is_same_v<T, int16_t>) {
      const hn::Rebind<uint16_t, decltype(d)> du;
      const auto bits = hn::Or(hn::PromoteTo(du, middle), hn::ShiftLeft<8>(hn::PromoteTo(du, high)));
      hn::StoreU(hn::BitCast(d, bits), d, out + i);
    } else {
      const hn::Rebind<uint32_t, decltype(d)> du;
      const hn::Rebind<int32_t, decltype(d)> di;
      const auto bits = hn::Or(hn::ShiftLeft<8>(hn::PromoteTo(du, low)),
          hn::Or(hn::ShiftLeft<16>(hn::PromoteTo(du, middle)), hn::ShiftLeft<24>(hn::PromoteTo(du, high))));
      const auto value = hn::BitCast(di, bits);
      if constexpr (std::is_same_v<T, float>)
        hn::StoreU(hn::Mul(hn::ConvertTo(d, value), hn::Set(d, 1.0f / 2147483648.0f)), d, out + i);
      else
        hn::StoreU(value, d, out + i);
    }
  }
  for (; i < count; ++i) out[i] = UnpackS24Sample<T>(in + 3 * i);
}

template<class T>
HWY_INLINE void PackS24(const T* in, uint8_t* out, size_t count) {
  const hn::ScalableTag<T> d;
  const hn::Rebind<uint8_t, decltype(d)> d8;
  const size_t n = hn::Lanes(d);
  const size_t end = count - count % n;
  size_t i = 0;
  for (; i < end; i += n) {
    const auto value = hn::LoadU(d, in + i);
    auto low = hn::Zero(d8), middle = low, high = low;
    if constexpr (std::is_same_v<T, uint8_t>) {
      high = hn::Xor(value, hn::Set(d8, 0x80));
    } else if constexpr (std::is_same_v<T, int16_t>) {
      const hn::Rebind<uint16_t, decltype(d)> du;
      const auto bits = hn::BitCast(du, value);
      middle = hn::TruncateTo(d8, bits);
      high = hn::TruncateTo(d8, hn::ShiftRight<8>(bits));
    } else {
      const hn::Rebind<uint32_t, decltype(d)> du;
      auto bits = hn::Zero(du);
      if constexpr (std::is_same_v<T, float>) bits = hn::BitCast(du, QuantizeForPacked24(d, value));
      else bits = hn::BitCast(du, value);
      low = hn::TruncateTo(d8, hn::ShiftRight<8>(bits));
      middle = hn::TruncateTo(d8, hn::ShiftRight<16>(bits));
      high = hn::TruncateTo(d8, hn::ShiftRight<24>(bits));
    }
    hn::StoreInterleaved3(low, middle, high, d8, out + i * 3);
  }
  for (; i < count; ++i) PackS24Sample(in[i], out + 3 * i);
}
#else
// Sixteen samples occupy exactly three packed vectors. Generate each shuffle
// from the sample layout, sharing the same loop and byte routing for all types.
using PackedTag = hn::FixedTag<uint8_t, 16>;
using PackedVector = hn::Vec<PackedTag>;

template<class T, bool Pack>
constexpr int SourceByte(int output) {
  constexpr int width = sizeof(T);
  int sample = 0, byte = 0;
  if constexpr (Pack) {
    sample = output / 3;
    byte = output % 3 + width - 3;
    if (byte < 0) return -1;
    if (HWY_IS_BIG_ENDIAN) byte = width - 1 - byte;
    return sample * width + byte;
  } else {
    sample = output / width;
    byte = output % width;
    if (HWY_IS_BIG_ENDIAN) byte = width - 1 - byte;
    byte += 3 - width;
    return byte < 0 ? -1 : sample * 3 + byte;
  }
}

template<class T, bool Pack, size_t Output, size_t Input>
HWY_INLINE PackedVector LoadShuffleMask() {
  alignas(16) static constexpr auto mask = [] {
    std::array<uint8_t, 16> bytes{};
    for (int j = 0; j < 16; ++j) {
      const int source = SourceByte<T, Pack>(int(Output * 16) + j);
      bytes[j] = source >= int(Input * 16) && source < int((Input + 1) * 16)
          ? uint8_t(source % 16) : uint8_t(0x80);
    }
    return bytes;
  }();
  return hn::Load(PackedTag(), mask.data());
}

template<class T, bool Pack, size_t... K>
HWY_INLINE auto LoadShuffleMasks(std::index_sequence<K...>) {
  constexpr size_t inputs = Pack ? sizeof(T) : 3;
  return std::array<PackedVector, sizeof...(K)>{LoadShuffleMask<T, Pack, K / inputs, K % inputs>()...};
}

template<class T, bool Pack, size_t Output, size_t Input>
HWY_INLINE PackedVector ShufflePart(PackedVector input, PackedVector mask) {
  constexpr bool used = [] {
    for (int j = 0; j < 16; ++j) {
      const int source = SourceByte<T, Pack>(int(Output * 16) + j);
      if (source >= int(Input * 16) && source < int((Input + 1) * 16)) return true;
    }
    return false;
  }();
  if constexpr (used)
    return hn::TableLookupBytesOr0(input, mask);
  else
    return hn::Zero(PackedTag());
}

template<class T, bool Pack, size_t Output, size_t... Input>
HWY_INLINE PackedVector ShuffleBlock(const PackedVector* input, const PackedVector* masks, std::index_sequence<Input...>) {
  auto value = hn::Zero(PackedTag());
  ((value = hn::Or(value, ShufflePart<T, Pack, Output, Input>(input[Input], masks[Output * sizeof...(Input) + Input]))), ...);
  return value;
}

template<class T, size_t K>
HWY_INLINE void UnpackVector(const PackedVector* input, const PackedVector* masks, T* out) {
  const hn::FixedTag<T, 16 / sizeof(T)> d;
  auto bytes = ShuffleBlock<T, false, K>(input, masks, std::make_index_sequence<3>());
  if constexpr (std::is_same_v<T, uint8_t>) bytes = hn::Xor(bytes, hn::Set(PackedTag(), 0x80));
  if constexpr (std::is_same_v<T, float>) {
    const hn::Rebind<int32_t, decltype(d)> di;
    hn::StoreU(hn::Mul(hn::ConvertTo(d, hn::BitCast(di, bytes)),
                      hn::Set(d, 1.0f / 2147483648.0f)), d, out + K * 4);
  } else {
    hn::StoreU(hn::BitCast(d, bytes), d, out + K * (16 / sizeof(T)));
  }
}

template<class T, size_t... K>
HWY_INLINE void UnpackBlock(const PackedVector* input, const PackedVector* masks, T* out, std::index_sequence<K...>) {
  (UnpackVector<T, K>(input, masks, out), ...);
}

template<class T, size_t K>
HWY_INLINE PackedVector LoadTypedVector(const T* in) {
  const hn::FixedTag<T, 16 / sizeof(T)> d;
  const auto value = hn::LoadU(d, in + K * (16 / sizeof(T)));
  if constexpr (std::is_same_v<T, float>) return hn::BitCast(PackedTag(), QuantizeForPacked24(d, value));
  else if constexpr (std::is_same_v<T, uint8_t>) return hn::Xor(value, hn::Set(d, 0x80));
  else return hn::BitCast(PackedTag(), value);
}

template<class T, size_t... K>
HWY_INLINE void PackBlock(const T* in, const PackedVector* masks, uint8_t* out, std::index_sequence<K...>) {
  PackedVector input[sizeof(T)];
#if HWY_MAX_BYTES >= 32 && (!defined(_MSC_VER) || defined(__clang__))
  if constexpr (std::is_same_v<T, float>) {
    // Quantize eight samples at once, then reuse the packed-byte mapping.
    // Explicit widths also keep wider Highway targets within this block.
    // MSVC's baseline-ISA TU spills these mixed-width values, so it uses
    // the 128-bit path below until it can generate comparable code here.
    const hn::FixedTag<float, 8> df;
    const hn::FixedTag<int32_t, 4> di;
    const auto lo = QuantizeForPacked24(df, hn::LoadU(df, in));
    const auto hi = QuantizeForPacked24(df, hn::LoadU(df, in + 8));
    input[0] = hn::BitCast(PackedTag(), hn::LowerHalf(di, lo));
    input[1] = hn::BitCast(PackedTag(), hn::UpperHalf(di, lo));
    input[2] = hn::BitCast(PackedTag(), hn::LowerHalf(di, hi));
    input[3] = hn::BitCast(PackedTag(), hn::UpperHalf(di, hi));
  } else
#endif
  {
    ((input[K] = LoadTypedVector<T, K>(in)), ...);
  }
  hn::StoreU(ShuffleBlock<T, true, 0>(input, masks, std::index_sequence<K...>()), PackedTag(), out);
  hn::StoreU(ShuffleBlock<T, true, 1>(input, masks, std::index_sequence<K...>()), PackedTag(), out + 16);
  hn::StoreU(ShuffleBlock<T, true, 2>(input, masks, std::index_sequence<K...>()), PackedTag(), out + 32);
}

template<class T>
HWY_INLINE void UnpackS24(const uint8_t* in, T* out, size_t count) {
  const auto masks = LoadShuffleMasks<T, false>(std::make_index_sequence<3 * sizeof(T)>());
  const size_t vector_end = count & ~size_t(15);
  size_t i = 0;
  for (; i < vector_end; i += 16) {
    const auto* src = in + 3 * i;
    const PackedVector input[] = {hn::LoadU(PackedTag(), src),
        hn::LoadU(PackedTag(), src + 16), hn::LoadU(PackedTag(), src + 32)};
    UnpackBlock(input, masks.data(), out + i, std::make_index_sequence<sizeof(T)>());
  }
  for (; i < count; ++i) out[i] = UnpackS24Sample<T>(in + 3 * i);
}

template<class T>
HWY_INLINE void PackS24(const T* in, uint8_t* out, size_t count) {
  const size_t vector_end = count & ~size_t(15);
  size_t i = 0;
  const auto masks = LoadShuffleMasks<T, true>(std::make_index_sequence<3 * sizeof(T)>());
  for (; i < vector_end; i += 16)
    PackBlock(in + i, masks.data(), out + 3 * i, std::make_index_sequence<sizeof(T)>());
  for (; i < count; ++i) PackS24Sample(in[i], out + 3 * i);
}

#endif  // Scalable or sizeless vectors

// -----------------------------------------------------------------------------
// S24 Wrappers
// -----------------------------------------------------------------------------

void convert24To16_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  UnpackS24(static_cast<const uint8_t*>(inbuf), static_cast<int16_t*>(outbuf), static_cast<size_t>(count));
}

void convert16To24_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  PackS24(static_cast<const int16_t*>(inbuf), static_cast<uint8_t*>(outbuf), static_cast<size_t>(count));
}

void convert24To8_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  UnpackS24(static_cast<const uint8_t*>(inbuf), static_cast<uint8_t*>(outbuf), static_cast<size_t>(count));
}

void convert8To24_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  PackS24(static_cast<const uint8_t*>(inbuf), static_cast<uint8_t*>(outbuf), static_cast<size_t>(count));
}

void convert24To32_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  UnpackS24(static_cast<const uint8_t*>(inbuf), static_cast<int32_t*>(outbuf), static_cast<size_t>(count));
}

void convert32To24_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  PackS24(static_cast<const int32_t*>(inbuf), static_cast<uint8_t*>(outbuf), static_cast<size_t>(count));
}

void convert24ToFLT_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  UnpackS24(static_cast<const uint8_t*>(inbuf), static_cast<float*>(outbuf), static_cast<size_t>(count));
}

void convertFLTTo24_HWY(void* inbuf, void* outbuf, int count) {
  if (count <= 0) return;
  PackS24(static_cast<const float*>(inbuf), static_cast<uint8_t*>(outbuf), static_cast<size_t>(count));
}

// -----------------------------------------------------------------------------
// Non-S24 Basic Integer Conversions
// -----------------------------------------------------------------------------

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
}  // namespace avs_audio_convert
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
#include "avs_simd/target_policy.h"

namespace avs_audio_convert {

struct RouteTable {
  convert_proc s32_to_s16;
  convert_proc s16_to_s32;
  convert_proc s32_to_u8;
  convert_proc u8_to_s32;
  convert_proc s16_to_u8;
  convert_proc u8_to_s16;

  convert_proc s32_to_s24;
  convert_proc s24_to_s32;
  convert_proc s24_to_s16;
  convert_proc s16_to_s24;
  convert_proc s24_to_u8;
  convert_proc u8_to_s24;

  convert_proc s24_to_f32;
  convert_proc f32_to_s24;
};

static const RouteTable kTableC = {
  convert32To16,
  convert16To32,
  convert32To8,
  convert8To32,
  convert16To8,
  convert8To16,

  convert32To24,
  convert24To32,
  convert24To16,
  convert16To24,
  convert24To8,
  convert8To24,

  nullptr,
  nullptr,
};

#define MAKE_ROUTE_TABLE(TARGET_MACRO) \
  { \
    TARGET_MACRO(convert32To16_HWY), \
    TARGET_MACRO(convert16To32_HWY), \
    TARGET_MACRO(convert32To8_HWY),  \
    TARGET_MACRO(convert8To32_HWY),  \
    TARGET_MACRO(convert16To8_HWY),  \
    TARGET_MACRO(convert8To16_HWY),  \
    TARGET_MACRO(convert32To24_HWY), \
    TARGET_MACRO(convert24To32_HWY), \
    TARGET_MACRO(convert24To16_HWY), \
    TARGET_MACRO(convert16To24_HWY), \
    TARGET_MACRO(convert24To8_HWY),  \
    TARGET_MACRO(convert8To24_HWY),  \
    TARGET_MACRO(convert24ToFLT_HWY),\
    TARGET_MACRO(convertFLTTo24_HWY) \
  }

static const RouteTable* GetRouteTableForTarget(int64_t target) {
  switch (target) {
#define AVS_SIMD_TARGET(target, choose) \
    case target: { \
      static const RouteTable table = MAKE_ROUTE_TABLE(choose); \
      return &table; \
    }
#include "avs_simd/targets.inc"
#undef AVS_SIMD_TARGET

    default:
      break;
  }
  return &kTableC;
}

#undef MAKE_ROUTE_TABLE

using RouteMember = convert_proc RouteTable::*;

static RouteMember FindRoute(int src_format, int dst_format) {
  const int route = (src_format << 16) | dst_format;
  switch (route) {
    case (SAMPLE_INT32 << 16) | SAMPLE_INT16: return &RouteTable::s32_to_s16;
    case (SAMPLE_INT16 << 16) | SAMPLE_INT32: return &RouteTable::s16_to_s32;
    case (SAMPLE_INT32 << 16) | SAMPLE_INT8:  return &RouteTable::s32_to_u8;
    case (SAMPLE_INT8  << 16) | SAMPLE_INT32: return &RouteTable::u8_to_s32;
    case (SAMPLE_INT16 << 16) | SAMPLE_INT8:  return &RouteTable::s16_to_u8;
    case (SAMPLE_INT8  << 16) | SAMPLE_INT16: return &RouteTable::u8_to_s16;

    case (SAMPLE_INT32 << 16) | SAMPLE_INT24: return &RouteTable::s32_to_s24;
    case (SAMPLE_INT24 << 16) | SAMPLE_INT32: return &RouteTable::s24_to_s32;
    case (SAMPLE_INT24 << 16) | SAMPLE_INT16: return &RouteTable::s24_to_s16;
    case (SAMPLE_INT16 << 16) | SAMPLE_INT24: return &RouteTable::s16_to_s24;
    case (SAMPLE_INT24 << 16) | SAMPLE_INT8:  return &RouteTable::s24_to_u8;
    case (SAMPLE_INT8  << 16) | SAMPLE_INT24: return &RouteTable::u8_to_s24;

    case (SAMPLE_INT24 << 16) | SAMPLE_FLOAT: return &RouteTable::s24_to_f32;
    case (SAMPLE_FLOAT << 16) | SAMPLE_INT24: return &RouteTable::f32_to_s24;

    default: return nullptr;
  }
}

convert_proc ResolveHighwayAudioConvertForTarget(int src_format, int dst_format, int64_t target) {
  const auto member = FindRoute(src_format, dst_format);
  return member ? GetRouteTableForTarget(target)->*member : nullptr;
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
  return FindRoute(src_format, dst_format) != nullptr;
}

}  // namespace avs_audio_convert
#endif  // HWY_ONCE
