#include <gtest/gtest.h>

#include "overlay_chroma_test_helpers.h"

#include "support/cpu_features.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

std::vector<OverlayChromaCase> overlay_chroma_cases() {
  return {
      make_overlay_chroma_case(OverlayChromaOperation::Yv12ToYv24, OverlayChromaSampleKind::U8, 19,
                               5, 64, 64,
                               Variant<OverlayChromaFunction>{
                                   "sse2", conv_yv12_to_yv24_chroma_u8_sse2, IsaRequirement::Sse2},
                               "158820401ef4f4d2"),
      make_overlay_chroma_case(OverlayChromaOperation::Yv12ToYv24,
                               OverlayChromaSampleKind::U16FullRange, 11, 5, 64, 64,
                               Variant<OverlayChromaFunction>{
                                   "sse2", conv_yv12_to_yv24_chroma_u16_sse2, IsaRequirement::Sse2},
                               "4ab6996e2ef0de37"),
      make_overlay_chroma_case(OverlayChromaOperation::Yv16ToYv24, OverlayChromaSampleKind::U8, 19,
                               5, 64, 64,
                               Variant<OverlayChromaFunction>{
                                   "sse2", conv_yv16_to_yv24_chroma_u8_sse2, IsaRequirement::Sse2},
                               "0b917fd78b43c4a7"),
      make_overlay_chroma_case(OverlayChromaOperation::Yv16ToYv24,
                               OverlayChromaSampleKind::U16FullRange, 11, 5, 64, 64,
                               Variant<OverlayChromaFunction>{
                                   "sse2", conv_yv16_to_yv24_chroma_u16_sse2, IsaRequirement::Sse2},
                               "fbf3e1d7f613939a"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U8, 106, 10, 128, 64,
          Variant<OverlayChromaFunction>{"ssse3", convert_yv24_chroma_to_yv12_u8_ssse3,
                                         IsaRequirement::Ssse3},
          "326c3dee157fe503"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U8, 106, 10, 128, 64,
          Variant<OverlayChromaFunction>{"avx2", convert_yv24_chroma_to_yv12_u8_avx2,
                                         IsaRequirement::Avx2},
          "326c3dee157fe503"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U8, 38, 6, 96, 40,
          Variant<OverlayChromaFunction>{"ssse3", convert_yv24_chroma_to_yv12_u8_ssse3,
                                         IsaRequirement::Ssse3},
          "01d1322fe5eb8598", 0xF30C1202U),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U8, 38, 6, 96, 40,
          Variant<OverlayChromaFunction>{"avx2", convert_yv24_chroma_to_yv12_u8_avx2,
                                         IsaRequirement::Avx2},
          "01d1322fe5eb8598", 0xF30C1202U),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U16LessThan16Bit, 54, 10,
          128, 64,
          Variant<OverlayChromaFunction>{"sse2", conv_yv24_to_yv12_chroma_u16_lessthan16bit_sse2,
                                         IsaRequirement::Sse2},
          "7ddb05589fa751b7"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U16LessThan16Bit, 54, 10,
          128, 64,
          Variant<OverlayChromaFunction>{
              "ssse3", convert_yv24_chroma_to_yv12_u16_lessthan16bit_ssse3, IsaRequirement::Ssse3},
          "7ddb05589fa751b7"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U16LessThan16Bit, 54, 10,
          128, 64,
          Variant<OverlayChromaFunction>{"avx2", convert_yv24_chroma_to_yv12_u16_lessthan16bit_avx2,
                                         IsaRequirement::Avx2},
          "7ddb05589fa751b7"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U16FullRange, 54, 10, 128,
          64,
          Variant<OverlayChromaFunction>{"sse2", conv_yv24_to_yv12_chroma_u16_true16bit_sse2,
                                         IsaRequirement::Sse2},
          "eb78b8b93c25bc15"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U16FullRange, 54, 10, 128,
          64,
          Variant<OverlayChromaFunction>{"sse4.1", convert_yv24_chroma_to_yv12_u16_sse41,
                                         IsaRequirement::Sse41},
          "eb78b8b93c25bc15"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U16FullRange, 54, 10, 128,
          64,
          Variant<OverlayChromaFunction>{"avx2", convert_yv24_chroma_to_yv12_u16_avx2,
                                         IsaRequirement::Avx2},
          "eb78b8b93c25bc15"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U16FullRange, 38, 6, 128,
          64,
          Variant<OverlayChromaFunction>{"sse2", conv_yv24_to_yv12_chroma_u16_true16bit_sse2,
                                         IsaRequirement::Sse2},
          "a4195329e6836a1f", 0xF30C1203U),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U16FullRange, 38, 6, 128,
          64,
          Variant<OverlayChromaFunction>{"sse4.1", convert_yv24_chroma_to_yv12_u16_sse41,
                                         IsaRequirement::Sse41},
          "a4195329e6836a1f", 0xF30C1203U),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::U16FullRange, 38, 6, 128,
          64,
          Variant<OverlayChromaFunction>{"avx2", convert_yv24_chroma_to_yv12_u16_avx2,
                                         IsaRequirement::Avx2},
          "a4195329e6836a1f", 0xF30C1203U),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::Float, 10, 10, 96, 64,
          Variant<OverlayChromaFunction>{"sse2", convert_yv24_chroma_to_yv12_float_sse2,
                                         IsaRequirement::Sse2},
          ""),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv12, OverlayChromaSampleKind::Float, 30, 12, 128, 80,
          Variant<OverlayChromaFunction>{"sse2", convert_yv24_chroma_to_yv12_float_sse2,
                                         IsaRequirement::Sse2},
          "", 0xF30C1201U),
      make_overlay_chroma_case(OverlayChromaOperation::Yv24ToYv16, OverlayChromaSampleKind::U8, 38,
                               5, 64, 64,
                               Variant<OverlayChromaFunction>{
                                   "sse2", conv_yv24_to_yv16_chroma_u8_sse2, IsaRequirement::Sse2},
                               "1b13dd9a012553fd"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv16, OverlayChromaSampleKind::U8, 38, 5, 64, 64,
          Variant<OverlayChromaFunction>{"sse4.1", conv_yv24_to_yv16_chroma_u8_sse41,
                                         IsaRequirement::Sse41},
          "1b13dd9a012553fd"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv16, OverlayChromaSampleKind::U8, 38, 6, 96, 48,
          Variant<OverlayChromaFunction>{"sse2", conv_yv24_to_yv16_chroma_u8_sse2,
                                         IsaRequirement::Sse2},
          "454ccddb2cca684e", 0xF30C1603U),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv16, OverlayChromaSampleKind::U8, 38, 6, 96, 48,
          Variant<OverlayChromaFunction>{"sse4.1", conv_yv24_to_yv16_chroma_u8_sse41,
                                         IsaRequirement::Sse41},
          "454ccddb2cca684e", 0xF30C1603U),
      make_overlay_chroma_case(OverlayChromaOperation::Yv24ToYv16,
                               OverlayChromaSampleKind::U16FullRange, 22, 5, 64, 64,
                               Variant<OverlayChromaFunction>{
                                   "sse2", conv_yv24_to_yv16_chroma_u16_sse2, IsaRequirement::Sse2},
                               "3a6a6534103bde13"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv16, OverlayChromaSampleKind::U16FullRange, 22, 5, 64, 64,
          Variant<OverlayChromaFunction>{"sse4.1", conv_yv24_to_yv16_chroma_u16_sse41,
                                         IsaRequirement::Sse41},
          "3a6a6534103bde13"),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv16, OverlayChromaSampleKind::U16FullRange, 38, 6, 128,
          64,
          Variant<OverlayChromaFunction>{"sse2", conv_yv24_to_yv16_chroma_u16_sse2,
                                         IsaRequirement::Sse2},
          "e764392d57f31b5e", 0xF30C1604U),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv16, OverlayChromaSampleKind::U16FullRange, 38, 6, 128,
          64,
          Variant<OverlayChromaFunction>{"sse4.1", conv_yv24_to_yv16_chroma_u16_sse41,
                                         IsaRequirement::Sse41},
          "e764392d57f31b5e", 0xF30C1604U),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv16, OverlayChromaSampleKind::Float, 10, 5, 64, 64,
          Variant<OverlayChromaFunction>{"sse2", convert_yv24_chroma_to_yv16_float_sse2,
                                         IsaRequirement::Sse2},
          ""),
      make_overlay_chroma_case(
          OverlayChromaOperation::Yv24ToYv16, OverlayChromaSampleKind::Float, 30, 7, 128, 80,
          Variant<OverlayChromaFunction>{"sse2", convert_yv24_chroma_to_yv16_float_sse2,
                                         IsaRequirement::Sse2},
          "", 0xF30C1602U),
  };
}

class OverlayChromaKernels : public ::testing::TestWithParam<OverlayChromaCase> {};

TEST_P(OverlayChromaKernels, MatchesIndependentResamplingReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  switch (test_case.sample_kind) {
    case OverlayChromaSampleKind::U8:
      run_overlay_chroma_case<std::uint8_t>(test_case);
      return;
    case OverlayChromaSampleKind::U16LessThan16Bit:
    case OverlayChromaSampleKind::U16FullRange:
      run_overlay_chroma_case<std::uint16_t>(test_case);
      return;
    case OverlayChromaSampleKind::Float:
      run_overlay_chroma_case<float>(test_case);
      return;
  }
}

INSTANTIATE_TEST_SUITE_P(ChromaResampling, OverlayChromaKernels,
                         ::testing::ValuesIn(overlay_chroma_cases()),
                         [](const ::testing::TestParamInfo<OverlayChromaCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
