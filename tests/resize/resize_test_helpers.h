#pragma once

#include "filters/resample_functions.h"
#ifndef AVS_UNUSED
// resample.h uses this macro after avisynth.h undefines it at the header end.
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_RESAMPLE_UNDEF_AVS_UNUSED
#endif
#include "filters/resample.h"
#ifdef AVSUT_RESAMPLE_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_RESAMPLE_UNDEF_AVS_UNUSED
#endif
#include "filters/intel/resample_avx2.h"
#include "filters/intel/resample_sse.h"
#include "filters/intel/resize_sse.h"

template<typename T, bool lessthan16bit>
inline void resizer_h_ssse3_generic_uint8_16(BYTE* dst, const BYTE* src, int dst_pitch, int src_pitch, ResamplingProgram* program, int width, int height, int bits_per_pixel) {
  if constexpr (sizeof(T) == 1) {
    resizer_h_ssse3_generic(dst, src, dst_pitch, src_pitch, program, width, height, bits_per_pixel);
  } else {
    resizer_h_ssse3_generic_uint16_t<lessthan16bit>(dst, src, dst_pitch, src_pitch, program, width, height, bits_per_pixel);
  }
}

#include "support/comparators.h"
#include "support/avisynth_environment.h"
#include "support/deterministic_data.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace avsut::test {

using ResizeFunction = void (*)(BYTE*, const BYTE*, int, int, ResamplingProgram*, int, int, int);
using VerticalReduceFunction = void (*)(BYTE*, const BYTE*, int, int, std::size_t, std::size_t);

enum class ResizeFilter {
  Triangle,
  Lanczos3,
  Lanczos6,
  Lanczos10,
};

inline std::unique_ptr<ResamplingFunction> make_resize_filter(ResizeFilter filter) {
  switch (filter) {
    case ResizeFilter::Triangle:
      return std::make_unique<TriangleFilter>();
    case ResizeFilter::Lanczos3:
      return std::make_unique<LanczosFilter>(3);
    case ResizeFilter::Lanczos6:
      return std::make_unique<LanczosFilter>(6);
    case ResizeFilter::Lanczos10:
      return std::make_unique<LanczosFilter>(10);
  }
  throw std::invalid_argument("unsupported resize filter");
}

inline const char* resize_filter_name(ResizeFilter filter) {
  switch (filter) {
    case ResizeFilter::Triangle:
      return "Triangle";
    case ResizeFilter::Lanczos3:
      return "Lanczos3";
    case ResizeFilter::Lanczos6:
      return "Lanczos6";
    case ResizeFilter::Lanczos10:
      return "Lanczos10";
  }
  return "Unknown";
}

struct Avx512HorizontalCoefficientLayout {
  int samples_in_group{};
  int group_count{};
  int prepared_filter_size{};
  int expected_filter_size_real{};
};

class ResamplingProgramOwner {
 public:
  ResamplingProgramOwner(int source_size, int target_size, int bits_per_pixel,
                         int coefficient_alignment = 16,
                         ResizeFilter filter = ResizeFilter::Triangle)
      : environment_(),
        filter_(make_resize_filter(filter)),
        program_(filter_->GetResamplingProgram(source_size, 0.0, static_cast<double>(source_size),
                                               target_size, bits_per_pixel, 0.5, 0.5,
                                               environment_.get())) {
    if (!program_) {
      throw std::runtime_error("GetResamplingProgram returned null");
    }
    resize_prepare_coeffs(program_.get(), environment_.get(), coefficient_alignment);
  }

  ~ResamplingProgramOwner() = default;

  ResamplingProgramOwner(const ResamplingProgramOwner&) = delete;
  ResamplingProgramOwner& operator=(const ResamplingProgramOwner&) = delete;

  ResamplingProgram* get() const noexcept { return program_.get(); }

  void prepare_avx512_horizontal_coefficients(Avx512HorizontalCoefficientLayout) {}

  void prepare_avx512_horizontal_float_coefficients() {}

 private:
  AviSynthEnvironment environment_;
  std::unique_ptr<ResamplingFunction> filter_;
  std::unique_ptr<ResamplingProgram> program_;
};

struct ResizeVertical8Case {
  std::size_t width_pixels{};
  std::size_t source_height{};
  std::size_t target_height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<ResizeFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  ResizeFilter filter{ResizeFilter::Triangle};
  std::string name;
};

struct VerticalReduceCase {
  std::size_t width_bytes{};
  std::size_t source_height{};
  std::size_t target_height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<VerticalReduceFunction> variant;
  std::string expected_hash;
  std::string name;
};

struct ResizeVertical16Case {
  int bits_per_pixel{};
  std::size_t width_pixels{};
  std::size_t source_height{};
  std::size_t target_height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<ResizeFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  ResizeFilter filter{ResizeFilter::Triangle};
  std::string name;
};

struct ResizeHorizontal8Case {
  std::size_t source_width{};
  std::size_t target_width{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<ResizeFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::optional<Avx512HorizontalCoefficientLayout> avx512_coefficient_layout;
  ResizeFilter filter{ResizeFilter::Triangle};
  std::string name;
};

struct ResizeHorizontal16Case {
  int bits_per_pixel{};
  std::size_t source_width{};
  std::size_t target_width{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<ResizeFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::optional<Avx512HorizontalCoefficientLayout> avx512_coefficient_layout;
  ResizeFilter filter{ResizeFilter::Triangle};
  std::string name;
};

struct ResizeVerticalFloatCase {
  std::size_t width_pixels{};
  std::size_t source_height{};
  std::size_t target_height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<ResizeFunction> variant;
  std::uint32_t seed{};
  std::size_t allowed_output_tail_bytes{32};
  std::string name;
};

struct ResizeHorizontalFloatCase {
  std::size_t source_width{};
  std::size_t target_width{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<ResizeFunction> variant;
  ResizeFilter filter{ResizeFilter::Triangle};
  int expected_filter_size_real{};
  bool prepare_avx512_float_coefficients{};
  std::uint32_t seed{};
  std::size_t allowed_output_tail_bytes{32};
  std::string name;
};

template <typename Function>
std::string resize_variant_name(const Variant<Function>& variant) {
  std::string result = "Variant";
  bool capitalize = true;
  for (const char character : variant.name) {
    if (character == '_' || character == '-' || character == '.') {
      capitalize = true;
      continue;
    }
    result.push_back(capitalize && character >= 'a' && character <= 'z'
                         ? static_cast<char>(character - ('a' - 'A'))
                         : character);
    capitalize = false;
  }
  return result;
}

inline std::string resize_vertical8_case_name(std::size_t width_pixels, std::size_t source_height,
                                              std::size_t target_height, std::size_t source_pitch,
                                              std::size_t destination_pitch,
                                              ResizeFilter filter,
                                              const Variant<ResizeFunction>& variant,
                                              std::uint32_t seed) {
  std::ostringstream stream;
  stream << "Plane8Vertical_Width" << width_pixels << "_SourceHeight" << source_height
         << "_TargetHeight" << target_height << "_SrcPitch" << source_pitch << "_DstPitch"
         << destination_pitch;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << "_Filter" << resize_filter_name(filter) << "_"
         << (seed == 0 ? "PatternBoundaryRamp_" : "PatternFixedRandom_")
         << resize_variant_name(variant);
  return stream.str();
}

inline std::string vertical_reduce_case_name(const VerticalReduceCase& test_case) {
  std::ostringstream stream;
  stream << "VerticalReduce_WidthBytes" << test_case.width_bytes << "_SourceHeight"
         << test_case.source_height << "_TargetHeight" << test_case.target_height << "_SrcPitch"
         << test_case.source_pitch << "_DstPitch" << test_case.destination_pitch
         << "_PatternBoundaryRamp_" << resize_variant_name(test_case.variant);
  return stream.str();
}

inline std::string resize_vertical16_case_name(int bits_per_pixel, std::size_t width_pixels,
                                               std::size_t source_height, std::size_t target_height,
                                               std::size_t source_pitch,
                                               std::size_t destination_pitch,
                                               ResizeFilter filter,
                                               const Variant<ResizeFunction>& variant,
                                               std::uint32_t seed) {
  std::ostringstream stream;
  stream << "Plane16Vertical_Bits" << bits_per_pixel << "_Width" << width_pixels << "_SourceHeight"
         << source_height << "_TargetHeight" << target_height << "_SrcPitch" << source_pitch
         << "_DstPitch" << destination_pitch;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << "_Filter" << resize_filter_name(filter) << "_"
         << (seed == 0 ? "PatternBoundaryRamp_" : "PatternFixedRandom_")
         << resize_variant_name(variant);
  return stream.str();
}

inline std::string resize_horizontal8_case_name(std::size_t source_width, std::size_t target_width,
                                                std::size_t height, std::size_t source_pitch,
                                                std::size_t destination_pitch, ResizeFilter filter,
                                                const Variant<ResizeFunction>& variant,
                                                std::uint32_t seed) {
  std::ostringstream stream;
  stream << "Plane8Horizontal_SourceWidth" << source_width << "_TargetWidth" << target_width
         << "_Height" << height << "_SrcPitch" << source_pitch << "_DstPitch" << destination_pitch
         << "_Filter" << resize_filter_name(filter);
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternBoundaryRamp_" : "_PatternFixedRandom_")
         << resize_variant_name(variant);
  return stream.str();
}

inline std::string resize_horizontal16_case_name(int bits_per_pixel, std::size_t source_width,
                                                 std::size_t target_width, std::size_t height,
                                                 std::size_t source_pitch,
                                                 std::size_t destination_pitch, ResizeFilter filter,
                                                 const Variant<ResizeFunction>& variant,
                                                 std::uint32_t seed) {
  std::ostringstream stream;
  stream << "Plane16Horizontal_Bits" << bits_per_pixel << "_SourceWidth" << source_width
         << "_TargetWidth" << target_width << "_Height" << height << "_SrcPitch" << source_pitch
         << "_DstPitch" << destination_pitch << "_Filter" << resize_filter_name(filter);
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternBoundaryRamp_" : "_PatternFixedRandom_")
         << resize_variant_name(variant);
  return stream.str();
}

inline std::string resize_vertical_float_case_name(std::size_t width_pixels,
                                                   std::size_t source_height,
                                                   std::size_t target_height,
                                                   std::size_t source_pitch,
                                                   std::size_t destination_pitch,
                                                   const Variant<ResizeFunction>& variant,
                                                   std::uint32_t seed) {
  std::ostringstream stream;
  stream << "PlaneFloatVertical_Width" << width_pixels << "_SourceHeight" << source_height
         << "_TargetHeight" << target_height << "_SrcPitch" << source_pitch << "_DstPitch"
         << destination_pitch;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << "_FilterTriangle_PatternFiniteAnchors_" << resize_variant_name(variant);
  return stream.str();
}

inline std::string resize_horizontal_float_case_name(std::size_t source_width,
                                                     std::size_t target_width, std::size_t height,
                                                     std::size_t source_pitch,
                                                     std::size_t destination_pitch,
                                                     ResizeFilter filter,
                                                     const Variant<ResizeFunction>& variant,
                                                     std::uint32_t seed) {
  std::ostringstream stream;
  stream << "PlaneFloatHorizontal_SourceWidth" << source_width << "_TargetWidth" << target_width
         << "_Height" << height << "_SrcPitch" << source_pitch << "_DstPitch" << destination_pitch
         << "_Filter" << resize_filter_name(filter);
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << "_PatternFiniteAnchors_" << resize_variant_name(variant);
  return stream.str();
}

inline ResizeVertical8Case make_resize_vertical8_case(
    std::size_t width_pixels, std::size_t source_height, std::size_t target_height,
    std::size_t source_pitch, std::size_t destination_pitch, Variant<ResizeFunction> variant,
    std::string expected_hash = {}, std::uint32_t seed = 0,
    ResizeFilter filter = ResizeFilter::Triangle) {
  ResizeVertical8Case result{width_pixels,
                             source_height,
                             target_height,
                             source_pitch,
                             destination_pitch,
                             std::move(variant),
                             std::move(expected_hash),
                             seed,
                             filter,
                             {}};
  result.name =
      resize_vertical8_case_name(result.width_pixels, result.source_height, result.target_height,
                                 result.source_pitch, result.destination_pitch, result.filter,
                                 result.variant, result.seed);
  return result;
}

inline VerticalReduceCase make_vertical_reduce_case(
    std::size_t width_bytes, std::size_t source_height, std::size_t target_height,
    std::size_t source_pitch, std::size_t destination_pitch,
    Variant<VerticalReduceFunction> variant, std::string expected_hash = {}) {
  VerticalReduceCase result{width_bytes,
                            source_height,
                            target_height,
                            source_pitch,
                            destination_pitch,
                            std::move(variant),
                            std::move(expected_hash),
                            {}};
  result.name = vertical_reduce_case_name(result);
  return result;
}

inline ResizeVertical16Case make_resize_vertical16_case(
    int bits_per_pixel, std::size_t width_pixels, std::size_t source_height,
    std::size_t target_height, std::size_t source_pitch, std::size_t destination_pitch,
    Variant<ResizeFunction> variant, std::string expected_hash = {}, std::uint32_t seed = 0,
    ResizeFilter filter = ResizeFilter::Triangle) {
  ResizeVertical16Case result{bits_per_pixel,
                              width_pixels,
                              source_height,
                              target_height,
                              source_pitch,
                              destination_pitch,
                              std::move(variant),
                              std::move(expected_hash),
                              seed,
                              filter,
                              {}};
  result.name = resize_vertical16_case_name(
      result.bits_per_pixel, result.width_pixels, result.source_height, result.target_height,
      result.source_pitch, result.destination_pitch, result.filter, result.variant, result.seed);
  return result;
}

inline ResizeHorizontal8Case make_resize_horizontal8_case(
    std::size_t source_width, std::size_t target_width, std::size_t height,
    std::size_t source_pitch, std::size_t destination_pitch, Variant<ResizeFunction> variant,
    std::string expected_hash = {},
    std::optional<Avx512HorizontalCoefficientLayout> avx512_coefficient_layout = std::nullopt,
    ResizeFilter filter = ResizeFilter::Triangle, std::uint32_t seed = 0) {
  ResizeHorizontal8Case result{source_width,
                               target_width,
                               height,
                               source_pitch,
                               destination_pitch,
                               std::move(variant),
                               std::move(expected_hash),
                               seed,
                               std::move(avx512_coefficient_layout),
                               filter,
                               {}};
  result.name = resize_horizontal8_case_name(
      result.source_width, result.target_width, result.height, result.source_pitch,
      result.destination_pitch, result.filter, result.variant, result.seed);
  return result;
}

inline ResizeHorizontal16Case make_resize_horizontal16_case(
    int bits_per_pixel, std::size_t source_width, std::size_t target_width, std::size_t height,
    std::size_t source_pitch, std::size_t destination_pitch, Variant<ResizeFunction> variant,
    std::string expected_hash = {},
    std::optional<Avx512HorizontalCoefficientLayout> avx512_coefficient_layout = std::nullopt,
    ResizeFilter filter = ResizeFilter::Triangle, std::uint32_t seed = 0) {
  ResizeHorizontal16Case result{bits_per_pixel,
                                source_width,
                                target_width,
                                height,
                                source_pitch,
                                destination_pitch,
                                std::move(variant),
                                std::move(expected_hash),
                                seed,
                                std::move(avx512_coefficient_layout),
                                filter,
                                {}};
  result.name = resize_horizontal16_case_name(
      result.bits_per_pixel, result.source_width, result.target_width, result.height,
      result.source_pitch, result.destination_pitch, result.filter, result.variant, result.seed);
  return result;
}

inline ResizeVerticalFloatCase make_resize_vertical_float_case(
    std::size_t width_pixels, std::size_t source_height, std::size_t target_height,
    std::size_t source_pitch, std::size_t destination_pitch, Variant<ResizeFunction> variant,
    std::uint32_t seed = 0, std::size_t allowed_output_tail_bytes = 32) {
  ResizeVerticalFloatCase result{width_pixels,
                                 source_height,
                                 target_height,
                                 source_pitch,
                                 destination_pitch,
                                 std::move(variant),
                                 seed,
                                 allowed_output_tail_bytes,
                                 {}};
  result.name = resize_vertical_float_case_name(result.width_pixels, result.source_height,
                                                result.target_height, result.source_pitch,
                                                result.destination_pitch, result.variant,
                                                result.seed);
  return result;
}

inline ResizeHorizontalFloatCase make_resize_horizontal_float_case(
    std::size_t source_width, std::size_t target_width, std::size_t height,
    std::size_t source_pitch, std::size_t destination_pitch, Variant<ResizeFunction> variant,
    ResizeFilter filter = ResizeFilter::Triangle, int expected_filter_size_real = 0,
    bool prepare_avx512_float_coefficients = false, std::uint32_t seed = 0,
    std::size_t allowed_output_tail_bytes = 32) {
  ResizeHorizontalFloatCase result{source_width,
                                   target_width,
                                   height,
                                   source_pitch,
                                   destination_pitch,
                                   std::move(variant),
                                   filter,
                                   expected_filter_size_real,
                                   prepare_avx512_float_coefficients,
                                   seed,
                                   allowed_output_tail_bytes,
                                   {}};
  result.name = resize_horizontal_float_case_name(
      result.source_width, result.target_width, result.height, result.source_pitch,
      result.destination_pitch, result.filter, result.variant, result.seed);
  return result;
}

inline void PrintTo(const ResizeVertical8Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const VerticalReduceCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const ResizeVertical16Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const ResizeHorizontal8Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const ResizeHorizontal16Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const ResizeVerticalFloatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const ResizeHorizontalFloatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_resize_input(PlaneView<T> view, int bits_per_pixel, std::uint32_t seed = 0) {
  static_assert(!std::is_const_v<T>);
  using Value = std::remove_const_t<T>;
  const std::uint64_t max_value = (std::uint64_t{1} << bits_per_pixel) - 1U;
  const std::array<std::uint64_t, 10> anchors{0U,
                                              1U,
                                              max_value / 7U,
                                              max_value / 3U,
                                              max_value / 2U,
                                              max_value - 2U,
                                              max_value - 1U,
                                              max_value,
                                              17U,
                                              193U};

  if (seed != 0) {
    XorShift32 generator(seed);
    for (std::size_t y = 0; y < view.height(); ++y) {
      for (std::size_t x = 0; x < view.width(); ++x) {
        view.row(y)[x] = static_cast<Value>(generator.next() & max_value);
      }
    }
    return;
  }

  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      const std::size_t index = y * view.width() + x;
      const std::uint64_t value = (index % anchors.size() == 0U)
                                      ? anchors[(index / anchors.size()) % anchors.size()]
                                      : (x * 37U + y * 101U + index * 13U) % (max_value + 1U);
      view.row(y)[x] = static_cast<Value>(value);
    }
  }
}

inline void fill_resize_float_input(PlaneView<float> view, std::uint32_t seed = 0) {
  constexpr std::array<float, 11> anchors{-1024.0F, -64.0F, -1.0F, -0.125F, -0.001F, 0.0F,
                                          0.001F,   0.125F, 1.0F,  64.0F,   1024.0F};
  const auto anchor_offset = static_cast<std::size_t>(seed % anchors.size());
  const auto ramp_offset = static_cast<std::size_t>(seed % 29U);
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      const std::size_t index = y * view.width() + x;
      if ((index % 7U) == 0U) {
        view.row(y)[x] = anchors[(index / 7U + anchor_offset) % anchors.size()];
      } else {
        const int ramp =
            static_cast<int>((x * 37U + y * 101U + index * 13U + ramp_offset) % 29U) - 14;
        view.row(y)[x] = static_cast<float>(ramp) * 0.375F;
      }
    }
  }
}

template <typename T>
void apply_resize_vertical_reference(PlaneView<const T> source, PlaneView<T> destination,
                                     const ResamplingProgram& program, int bits_per_pixel) {
  static_assert(std::is_integral_v<T>);
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(destination.height(), static_cast<std::size_t>(program.target_size));

  constexpr bool is_8_bit = sizeof(T) == sizeof(std::uint8_t);
  const int scale_bits = is_8_bit ? FPScale8bits : FPScale16bits;
  const int rounder = 1 << (scale_bits - 1);
  const int limit = (1 << bits_per_pixel) - 1;

  for (std::size_t y = 0; y < destination.height(); ++y) {
    const int offset = program.pixel_offset[y];
    const short* coefficients = program.pixel_coefficient + y * program.filter_size;
    for (std::size_t x = 0; x < destination.width(); ++x) {
      int result = rounder;
      for (int i = 0; i < program.filter_size_real; ++i) {
        int value = source.row(static_cast<std::size_t>(offset + i))[x];
        if constexpr (sizeof(T) == sizeof(std::uint16_t)) {
          if (bits_per_pixel == 16) {
            value -= 32768;
          }
        }
        result += value * coefficients[i];
      }
      if constexpr (sizeof(T) == sizeof(std::uint16_t)) {
        if (bits_per_pixel == 16) {
          result += 32768 << FPScale16bits;
        }
      }
      result >>= scale_bits;
      destination.row(y)[x] = static_cast<T>(std::clamp(result, 0, limit));
    }
  }
}

inline void apply_vertical_reduce_reference(PlaneView<const std::uint8_t> source,
                                            PlaneView<std::uint8_t> destination) {
  for (std::size_t y = 0; y < destination.height(); ++y) {
    const auto source_y = y * 2;
    for (std::size_t x = 0; x < destination.width(); ++x) {
      const auto first = source.row(source_y)[x];
      const auto second = source.row(source_y + 1)[x];
      if (y + 1 < destination.height()) {
        const auto third = source.row(source_y + 2)[x];
        destination.row(y)[x] = static_cast<std::uint8_t>((first + 2 * second + third + 2) / 4);
      } else {
        destination.row(y)[x] = static_cast<std::uint8_t>((first + 3 * second + 2) / 4);
      }
    }
  }
}

template <typename T>
void invoke_resize_vertical(const ResizeFunction function, PlaneView<T> destination,
                            PlaneView<const T> source, ResamplingProgram* program,
                            int bits_per_pixel) {
  function(reinterpret_cast<BYTE*>(destination.data()),
           reinterpret_cast<const BYTE*>(source.data()),
           static_cast<int>(destination.pitch_bytes()), static_cast<int>(source.pitch_bytes()),
           program, static_cast<int>(destination.width()), static_cast<int>(destination.height()),
           bits_per_pixel);
}

template <typename T>
void apply_resize_horizontal_reference(PlaneView<const T> source, PlaneView<T> destination,
                                       const ResamplingProgram& program, int bits_per_pixel) {
  static_assert(std::is_integral_v<T>);
  const int scale_bits = sizeof(T) == sizeof(std::uint8_t) ? FPScale8bits : FPScale16bits;
  const int rounder = 1 << (scale_bits - 1);
  const int limit = (1 << bits_per_pixel) - 1;

  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); ++x) {
      const int offset = program.pixel_offset[x];
      const short* coefficients = program.pixel_coefficient + x * program.filter_size;
      int result = rounder;
      for (int i = 0; i < program.filter_size_real; ++i) {
        int value = source.row(y)[static_cast<std::size_t>(offset + i)];
        if constexpr (sizeof(T) == sizeof(std::uint16_t)) {
          if (bits_per_pixel == 16) {
            value -= 32768;
          }
        }
        result += value * coefficients[i];
      }
      if constexpr (sizeof(T) == sizeof(std::uint16_t)) {
        if (bits_per_pixel == 16) {
          result += 32768 << FPScale16bits;
        }
      }
      result >>= scale_bits;
      destination.row(y)[x] = static_cast<T>(std::clamp(result, 0, limit));
    }
  }
}

template <typename T>
void invoke_resize_horizontal(const ResizeFunction function, PlaneView<T> destination,
                              PlaneView<const T> source, ResamplingProgram* program,
                              int bits_per_pixel) {
  function(reinterpret_cast<BYTE*>(destination.data()),
           reinterpret_cast<const BYTE*>(source.data()),
           static_cast<int>(destination.pitch_bytes()), static_cast<int>(source.pitch_bytes()),
           program, static_cast<int>(destination.width()), static_cast<int>(destination.height()),
           bits_per_pixel);
}

template <typename T>
void run_resize_vertical_case(std::size_t width_pixels, std::size_t source_height,
                              std::size_t target_height, std::size_t source_pitch,
                              std::size_t destination_pitch, int bits_per_pixel,
                              const Variant<ResizeFunction>& variant,
                              const std::string& expected_hash, std::uint32_t seed,
                              ResizeFilter filter) {
  GuardedVideoBuffer<T> source(width_pixels, source_height, source_pitch, 64);
  GuardedVideoBuffer<T> expected(width_pixels, target_height, destination_pitch, 64);
  GuardedVideoBuffer<T> actual(width_pixels, target_height, destination_pitch, 64);
  fill_resize_input(source.view(), bits_per_pixel, seed);
  const auto source_snapshot = source.snapshot_active();

  ResamplingProgramOwner program(static_cast<int>(source_height), static_cast<int>(target_height),
                                 bits_per_pixel, 16, filter);
  apply_resize_vertical_reference(source.view().as_const(), expected.view(), *program.get(),
                                  bits_per_pixel);
  invoke_resize_vertical(variant.function, actual.view(), source.view().as_const(), program.get(),
                         bits_per_pixel);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << "variant=" << variant.name << " bits=" << bits_per_pixel << " width=" << width_pixels
      << " source_height=" << source_height << " target_height=" << target_height
      << " source_pitch=" << source_pitch << " destination_pitch=" << destination_pitch;
  const auto actual_hash = format_hash(hash_active(actual.view().as_const()));
  EXPECT_EQ(actual_hash, expected_hash)
      << "variant=" << variant.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << "variant=" << variant.name << " modified source pixels";
  EXPECT_TRUE(source.memory_intact())
      << "variant=" << variant.name << " corrupted source padding or guards";
  EXPECT_TRUE(expected.memory_intact())
      << "variant=" << variant.name << " corrupted reference padding or guards";
  EXPECT_TRUE(actual.memory_intact())
      << "variant=" << variant.name << " corrupted output padding or guards";
}

inline void run_resize_vertical8_case(const ResizeVertical8Case& test_case) {
  run_resize_vertical_case<std::uint8_t>(test_case.width_pixels, test_case.source_height,
                                         test_case.target_height, test_case.source_pitch,
                                         test_case.destination_pitch, 8, test_case.variant,
                                         test_case.expected_hash, test_case.seed,
                                         test_case.filter);
}

inline void run_vertical_reduce_case(const VerticalReduceCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.width_bytes, test_case.source_height,
                                          test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_bytes, test_case.target_height,
                                            test_case.destination_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width_bytes, test_case.target_height,
                                          test_case.destination_pitch, 64);
  fill_resize_input(source.view(), 8);
  const auto source_snapshot = source.snapshot_active();
  apply_vertical_reduce_reference(source.view().as_const(), expected.view());

  test_case.variant.function(reinterpret_cast<BYTE*>(actual.view().data()),
                             reinterpret_cast<const BYTE*>(source.view().data()),
                             static_cast<int>(actual.view().pitch_bytes()),
                             static_cast<int>(source.view().pitch_bytes()), test_case.width_bytes,
                             test_case.target_height);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(actual.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified source pixels";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " corrupted source padding or guards";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " corrupted reference padding or guards";
  EXPECT_TRUE(actual.memory_intact()) << test_case.name << " corrupted output padding or guards";
}

inline void run_resize_vertical16_case(const ResizeVertical16Case& test_case) {
  run_resize_vertical_case<std::uint16_t>(test_case.width_pixels, test_case.source_height,
                                          test_case.target_height, test_case.source_pitch,
                                          test_case.destination_pitch, test_case.bits_per_pixel,
                                          test_case.variant, test_case.expected_hash,
                                          test_case.seed, test_case.filter);
}

template <typename T>
void run_resize_horizontal_case(
    std::size_t source_width, std::size_t target_width, std::size_t height,
    std::size_t source_pitch, std::size_t destination_pitch, int bits_per_pixel,
    const Variant<ResizeFunction>& variant, const std::string& expected_hash,
    std::optional<Avx512HorizontalCoefficientLayout> avx512_coefficient_layout,
    ResizeFilter filter, std::uint32_t seed) {
  GuardedVideoBuffer<T> source(source_width, height, source_pitch, 64);
  GuardedVideoBuffer<T> expected(target_width, height, destination_pitch, 64);
  GuardedVideoBuffer<T> actual(target_width, height, destination_pitch, 64);
  fill_resize_input(source.view(), bits_per_pixel, seed);
  const auto source_snapshot = source.snapshot_active();

  ResamplingProgramOwner program(static_cast<int>(source_width), static_cast<int>(target_width),
                                 bits_per_pixel, 16, filter);
  if (avx512_coefficient_layout.has_value()) {
    if (avx512_coefficient_layout->expected_filter_size_real > 0) {
      ASSERT_EQ(program.get()->filter_size_real,
                avx512_coefficient_layout->expected_filter_size_real)
          << "variant=" << variant.name << " source_width=" << source_width
          << " target_width=" << target_width << " unexpected filter_size_real";
    }
    program.prepare_avx512_horizontal_coefficients(*avx512_coefficient_layout);
  }
  apply_resize_horizontal_reference(source.view().as_const(), expected.view(), *program.get(),
                                    bits_per_pixel);
  invoke_resize_horizontal(variant.function, actual.view(), source.view().as_const(), program.get(),
                           bits_per_pixel);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << "variant=" << variant.name << " bits=" << bits_per_pixel
      << " source_width=" << source_width << " target_width=" << target_width
      << " height=" << height << " source_pitch=" << source_pitch
      << " destination_pitch=" << destination_pitch;
  const auto actual_hash = format_hash(hash_active(actual.view().as_const()));
  EXPECT_EQ(actual_hash, expected_hash)
      << "variant=" << variant.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << "variant=" << variant.name << " modified source pixels";
  EXPECT_TRUE(source.memory_intact())
      << "variant=" << variant.name << " corrupted source padding or guards";
  EXPECT_TRUE(expected.memory_intact())
      << "variant=" << variant.name << " corrupted reference padding or guards";
  EXPECT_TRUE(actual.memory_intact())
      << "variant=" << variant.name << " corrupted output padding or guards";
}

inline void run_resize_horizontal8_case(const ResizeHorizontal8Case& test_case) {
  run_resize_horizontal_case<std::uint8_t>(
      test_case.source_width, test_case.target_width, test_case.height, test_case.source_pitch,
      test_case.destination_pitch, 8, test_case.variant, test_case.expected_hash,
      test_case.avx512_coefficient_layout, test_case.filter, test_case.seed);
}

inline void run_resize_horizontal16_case(const ResizeHorizontal16Case& test_case) {
  run_resize_horizontal_case<std::uint16_t>(
      test_case.source_width, test_case.target_width, test_case.height, test_case.source_pitch,
      test_case.destination_pitch, test_case.bits_per_pixel, test_case.variant,
      test_case.expected_hash, test_case.avx512_coefficient_layout, test_case.filter,
      test_case.seed);
}

inline std::uint64_t resize_float_ulp_distance(float lhs, float rhs) noexcept {
  if (lhs == rhs) {
    return 0;
  }
  std::uint32_t lhs_bits{};
  std::uint32_t rhs_bits{};
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  constexpr std::uint32_t sign_bit = 0x80000000U;
  if ((lhs_bits & sign_bit) != (rhs_bits & sign_bit)) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  const std::uint32_t lhs_magnitude = lhs_bits & ~sign_bit;
  const std::uint32_t rhs_magnitude = rhs_bits & ~sign_bit;
  return lhs_magnitude >= rhs_magnitude ? static_cast<std::uint64_t>(lhs_magnitude - rhs_magnitude)
                                        : static_cast<std::uint64_t>(rhs_magnitude - lhs_magnitude);
}

inline ::testing::AssertionResult compare_resize_float_ulp(PlaneView<const float> expected,
                                                           PlaneView<const float> actual,
                                                           std::uint64_t maximum_ulps = 4,
                                                           float absolute_floor = 1.0e-4F) {
  if (expected.width() != actual.width() || expected.height() != actual.height()) {
    return ::testing::AssertionFailure() << "dimension mismatch";
  }
  for (std::size_t y = 0; y < expected.height(); ++y) {
    for (std::size_t x = 0; x < expected.width(); ++x) {
      const float lhs = expected.row(y)[x];
      const float rhs = actual.row(y)[x];
      if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        return ::testing::AssertionFailure() << "row=" << y << " col=" << x
                                             << " non-finite expected=" << lhs << " actual=" << rhs;
      }
      if (lhs == rhs) {
        continue;
      }
      const float absolute_error = std::abs(lhs - rhs);
      const auto ulps = resize_float_ulp_distance(lhs, rhs);
      if (absolute_error <= absolute_floor || ulps <= maximum_ulps) {
        continue;
      }
      return ::testing::AssertionFailure()
             << "row=" << y << " col=" << x << " expected=" << lhs << " actual=" << rhs
             << " absolute_error=" << absolute_error << " ulps=" << ulps
             << " allowed_ulps=" << maximum_ulps << " absolute_floor=" << absolute_floor;
    }
  }
  return ::testing::AssertionSuccess();
}

inline void apply_resize_vertical_float_reference(PlaneView<const float> source,
                                                  PlaneView<float> destination,
                                                  const ResamplingProgram& program) {
  for (std::size_t y = 0; y < destination.height(); ++y) {
    const int offset = program.pixel_offset[y];
    const float* coefficients = program.pixel_coefficient_float + y * program.filter_size;
    for (std::size_t x = 0; x < destination.width(); ++x) {
      double result = 0.0;
      for (int i = 0; i < program.filter_size_real; ++i) {
        result +=
            static_cast<double>(source.row(offset + i)[x]) * static_cast<double>(coefficients[i]);
      }
      destination.row(y)[x] = static_cast<float>(result);
    }
  }
}

inline void apply_resize_horizontal_float_reference(PlaneView<const float> source,
                                                    PlaneView<float> destination,
                                                    const ResamplingProgram& program) {
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); ++x) {
      const int offset = program.pixel_offset[x];
      const float* coefficients = program.pixel_coefficient_float + x * program.filter_size;
      double result = 0.0;
      for (int i = 0; i < program.filter_size_real; ++i) {
        result +=
            static_cast<double>(source.row(y)[offset + i]) * static_cast<double>(coefficients[i]);
      }
      destination.row(y)[x] = static_cast<float>(result);
    }
  }
}

inline void expect_resize_float_output_memory(const std::string& name,
                                              const GuardedVideoBuffer<float>& actual,
                                              std::size_t width_pixels,
                                              std::size_t allowed_output_tail_bytes) {
  EXPECT_TRUE(actual.guards_intact()) << name << " output guards";
  const auto active_row_bytes = width_pixels * sizeof(float);
  const auto allowed_end = std::min(
      actual.view().pitch_bytes(),
      ((active_row_bytes + allowed_output_tail_bytes - 1) / allowed_output_tail_bytes) *
          allowed_output_tail_bytes);
  const bool permitted_tail_write =
      !actual.padding_intact() && actual.padding_intact_from(allowed_end);
  if (permitted_tail_write) {
    std::cout << "[INFO] " << name
              << " modified output padding within the permitted " << allowed_output_tail_bytes
              << "-byte SIMD tail"
              << " (active_row_bytes=" << active_row_bytes << ", protected_from_byte="
              << allowed_end << ")\n";
  }
  EXPECT_TRUE(actual.padding_intact_from(allowed_end))
      << name << " output padding after permitted " << allowed_output_tail_bytes
      << "-byte SIMD tail boundary " << allowed_end;
}

inline void run_resize_vertical_float_case(const ResizeVerticalFloatCase& test_case) {
  GuardedVideoBuffer<float> source(test_case.width_pixels, test_case.source_height,
                                   test_case.source_pitch, 64);
  GuardedVideoBuffer<float> expected(test_case.width_pixels, test_case.target_height,
                                     test_case.destination_pitch, 64);
  GuardedVideoBuffer<float> actual(test_case.width_pixels, test_case.target_height,
                                   test_case.destination_pitch, 64);
  fill_resize_float_input(source.view(), test_case.seed);
  const auto source_snapshot = source.snapshot_active();

  ResamplingProgramOwner program(static_cast<int>(test_case.source_height),
                                 static_cast<int>(test_case.target_height), 32);
  apply_resize_vertical_float_reference(source.view().as_const(), expected.view(), *program.get());
  invoke_resize_vertical(test_case.variant.function, actual.view(), source.view().as_const(),
                         program.get(), 32);

  EXPECT_TRUE(compare_resize_float_ulp(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified source pixels";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " corrupted source padding or guards";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " corrupted reference padding or guards";
  expect_resize_float_output_memory(test_case.name, actual, test_case.width_pixels,
                                    test_case.allowed_output_tail_bytes);
}

inline void run_resize_horizontal_float_case(const ResizeHorizontalFloatCase& test_case) {
  GuardedVideoBuffer<float> source(test_case.source_width, test_case.height, test_case.source_pitch,
                                   64);
  GuardedVideoBuffer<float> expected(test_case.target_width, test_case.height,
                                     test_case.destination_pitch, 64);
  GuardedVideoBuffer<float> actual(test_case.target_width, test_case.height,
                                   test_case.destination_pitch, 64);
  fill_resize_float_input(source.view(), test_case.seed);
  const auto source_snapshot = source.snapshot_active();

  ResamplingProgramOwner program(static_cast<int>(test_case.source_width),
                                 static_cast<int>(test_case.target_width), 32, 8, test_case.filter);
  if (test_case.expected_filter_size_real > 0) {
    ASSERT_EQ(program.get()->filter_size_real, test_case.expected_filter_size_real)
        << test_case.name << " unexpected filter_size_real";
  }
  apply_resize_horizontal_float_reference(source.view().as_const(), expected.view(),
                                          *program.get());
  if (test_case.prepare_avx512_float_coefficients) {
    program.prepare_avx512_horizontal_float_coefficients();
  }
  invoke_resize_horizontal(test_case.variant.function, actual.view(), source.view().as_const(),
                           program.get(), 32);

  EXPECT_TRUE(compare_resize_float_ulp(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified source pixels";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " corrupted source padding or guards";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " corrupted reference padding or guards";
  expect_resize_float_output_memory(test_case.name, actual, test_case.target_width,
                                    test_case.allowed_output_tail_bytes);
}

}  // namespace avsut::test
