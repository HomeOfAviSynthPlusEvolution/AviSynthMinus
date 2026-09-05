#include <gtest/gtest.h>

#include "convert_audio_test_helpers.h"
#include "kernels_highway.h"
#include "avs_simd/target_policy.h"
#include <avisynth.h>

#include "support/cpu_features.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>
#include <limits>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#endif


namespace avsut::test {
namespace {

inline int to_avisynth_sample_type(AudioFormat format) {
  switch (format) {
    case AudioFormat::U8: return SAMPLE_INT8;
    case AudioFormat::S16: return SAMPLE_INT16;
    case AudioFormat::S24: return SAMPLE_INT24;
    case AudioFormat::S32: return SAMPLE_INT32;
    case AudioFormat::F32: return SAMPLE_FLOAT;
  }
  return 0;
}

#ifdef INTEL_INTRINSICS
#define AUDIO_SIMD_KERNEL(function) function
#else
#define AUDIO_SIMD_KERNEL(function) static_cast<AudioConvertFunction>(nullptr)
#endif

template <typename Function>
void add_integer_variants(std::vector<AudioIntegerCase>& cases, AudioFormat source,
                          AudioFormat destination, std::size_t count, const char* expected_hash,
                          Function c_function) {
  cases.push_back(make_audio_integer_case(
      source, destination, count,
      Variant<AudioConvertFunction>{"c", c_function, IsaRequirement::Scalar}, expected_hash));

  const int src_type = to_avisynth_sample_type(source);
  const int dst_type = to_avisynth_sample_type(destination);
  const auto hwy_c = avs_audio_convert::ResolveHighwayAudioConvert(src_type, dst_type, 0);
  const auto hwy_native = avs_audio_convert::ResolveHighwayAudioConvert(src_type, dst_type, ~0);

  cases.push_back(make_audio_integer_case(
      source, destination, count,
      Variant<AudioConvertFunction>{"hwy_c", hwy_c, IsaRequirement::Scalar}, expected_hash));
  cases.push_back(make_audio_integer_case(
      source, destination, count,
      Variant<AudioConvertFunction>{"hwy_native", hwy_native, IsaRequirement::Scalar}, expected_hash));
}

TEST(AudioHighwayDispatch, AllIntegerRoutesResolveToCOrNative) {
  struct Route { int src; int dst; convert_proc scalar; };
  const Route routes[] = {
    {SAMPLE_INT8,  SAMPLE_INT16, convert8To16},
    {SAMPLE_INT16, SAMPLE_INT8,  convert16To8},
    {SAMPLE_INT8,  SAMPLE_INT32, convert8To32},
    {SAMPLE_INT32, SAMPLE_INT8,  convert32To8},
    {SAMPLE_INT16, SAMPLE_INT32, convert16To32},
    {SAMPLE_INT32, SAMPLE_INT16, convert32To16},
    {SAMPLE_INT32, SAMPLE_INT24, convert32To24},
    {SAMPLE_INT24, SAMPLE_INT32, convert24To32},
    {SAMPLE_INT24, SAMPLE_INT16, convert24To16},
    {SAMPLE_INT16, SAMPLE_INT24, convert16To24},
    {SAMPLE_INT24, SAMPLE_INT8,  convert24To8},
    {SAMPLE_INT8,  SAMPLE_INT24, convert8To24},
  };
  const auto target = avs_simd::ChooseTarget(~0, avs_audio_convert::GetHighwayAudioConvertCompiledTargets());
  EXPECT_EQ(avs_audio_convert::GetHighwayAudioConvertChosenTarget(~0), target);
  EXPECT_EQ(avs_audio_convert::GetHighwayAudioConvertChosenTarget(0), avs_simd::TARGET_C_FALLBACK);
  for (const auto& route : routes) {
    SCOPED_TRACE(::testing::Message() << route.src << " -> " << route.dst);
    EXPECT_EQ(avs_audio_convert::ResolveHighwayAudioConvert(route.src, route.dst, 0), route.scalar);
    const auto native = avs_audio_convert::ResolveHighwayAudioConvert(route.src, route.dst, ~0);
    ASSERT_NE(native, nullptr);
    if (target == avs_simd::TARGET_C_FALLBACK) {
      EXPECT_EQ(native, route.scalar);
    } else {
      EXPECT_NE(native, route.scalar);
      EXPECT_EQ(native, avs_audio_convert::ResolveHighwayAudioConvertForTarget(route.src, route.dst, target));
    }
  }

  // Direct float routes: S24 <-> F32
  // Zero flags resolves to nullptr (scalar fallback is composed two-stage in filter)
  EXPECT_EQ(avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_INT24, SAMPLE_FLOAT, 0), nullptr);
  EXPECT_EQ(avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_FLOAT, SAMPLE_INT24, 0), nullptr);
  const auto s24_to_f32_native = avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_INT24, SAMPLE_FLOAT, ~0);
  const auto f32_to_s24_native = avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_FLOAT, SAMPLE_INT24, ~0);
  if (target == avs_simd::TARGET_C_FALLBACK) {
    EXPECT_EQ(s24_to_f32_native, nullptr);
    EXPECT_EQ(f32_to_s24_native, nullptr);
  } else {
    ASSERT_NE(s24_to_f32_native, nullptr);
    ASSERT_NE(f32_to_s24_native, nullptr);
    EXPECT_EQ(s24_to_f32_native, avs_audio_convert::ResolveHighwayAudioConvertForTarget(SAMPLE_INT24, SAMPLE_FLOAT, target));
    EXPECT_EQ(f32_to_s24_native, avs_audio_convert::ResolveHighwayAudioConvertForTarget(SAMPLE_FLOAT, SAMPLE_INT24, target));
  }

  // Unsupported float routes return nullptr
  EXPECT_EQ(avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_INT8, SAMPLE_FLOAT, ~0), nullptr);
  EXPECT_EQ(avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_FLOAT, SAMPLE_INT8, ~0), nullptr);
  EXPECT_EQ(avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_INT16, SAMPLE_FLOAT, ~0), nullptr);
  EXPECT_EQ(avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_FLOAT, SAMPLE_INT16, ~0), nullptr);
  EXPECT_EQ(avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_INT32, SAMPLE_FLOAT, ~0), nullptr);
  EXPECT_EQ(avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_FLOAT, SAMPLE_INT32, ~0), nullptr);
}

std::vector<AudioIntegerCase> audio_integer_cases() {
  constexpr std::array<std::size_t, 6> counts{7, 8, 9, 15, 16, 17};
  constexpr std::array<const char*, 6> s32_to_s16{"72636ca0711e509d", "bc0055e922cca5c1",
                                                  "dddf915c0a598053", "7f85c2db07160a03",
                                                  "8583483f9c7f777d", "236d8ef6157ac922"};
  constexpr std::array<const char*, 6> s16_to_s32{"7f585b802d4eefde", "a709f5d101dab569",
                                                  "8493cdc5eac29749", "f58c1429ba6987ee",
                                                  "e75c50f543851658", "5fb431b11e75eca8"};
  constexpr std::array<const char*, 6> s32_to_u8{"2fa42ce33b9b9690", "26631036a0122595",
                                                 "5089246ee560804b", "94e50bd25b3fd8ca",
                                                 "4e7769dc192a2c6d", "20fa999e5e2c8324"};
  constexpr std::array<const char*, 6> u8_to_s32{"1365282cea4e33d3", "f53ba3e2b796b5a5",
                                                 "9e6d204fccb2fb77", "1e543f3519614fba",
                                                 "f2e1a143da816fa7", "5bd5391fc2326e74"};
  constexpr std::array<const char*, 6> s16_to_u8{"6d140c77ecf217a9", "825796fed5f9a30b",
                                                 "8fea9faa63e457e4", "f5cc9a5828084c2c",
                                                 "a9109668148bb410", "9b3d2c9927677728"};
  constexpr std::array<const char*, 6> u8_to_s16{"5f8296cbe98967a7", "b827603c85c112e3",
                                                 "256c7f8cff50d066", "65e83ea200c3bb22",
                                                 "e9f8dbb9828874e2", "730ae7733badca4d"};
  std::vector<AudioIntegerCase> cases;
  for (std::size_t index = 0; index < counts.size(); ++index) {
    const auto count = counts[index];
    add_integer_variants(cases, AudioFormat::S32, AudioFormat::S16, count, s32_to_s16[index], convert32To16);
    add_integer_variants(cases, AudioFormat::S16, AudioFormat::S32, count, s16_to_s32[index], convert16To32);
    add_integer_variants(cases, AudioFormat::S32, AudioFormat::U8, count, s32_to_u8[index], convert32To8);
    add_integer_variants(cases, AudioFormat::U8, AudioFormat::S32, count, u8_to_s32[index], convert8To32);
    add_integer_variants(cases, AudioFormat::S16, AudioFormat::U8, count, s16_to_u8[index], convert16To8);
    add_integer_variants(cases, AudioFormat::U8, AudioFormat::S16, count, u8_to_s16[index], convert8To16);
  }

  constexpr std::array<std::size_t, 8> edge_counts{0, 1, 2, 3, 31, 32, 33, 64};
  for (const auto count : edge_counts) {
    add_integer_variants(cases, AudioFormat::S32, AudioFormat::S16, count, "", convert32To16);
    add_integer_variants(cases, AudioFormat::S16, AudioFormat::S32, count, "", convert16To32);
    add_integer_variants(cases, AudioFormat::S32, AudioFormat::U8, count, "", convert32To8);
    add_integer_variants(cases, AudioFormat::U8, AudioFormat::S32, count, "", convert8To32);
    add_integer_variants(cases, AudioFormat::S16, AudioFormat::U8, count, "", convert16To8);
    add_integer_variants(cases, AudioFormat::U8, AudioFormat::S16, count, "", convert8To16);
  }

  return cases;
}

template <typename Function>
void add_packed_24_variants(std::vector<AudioIntegerCase>& cases, AudioFormat source,
                            AudioFormat destination, std::size_t count, const char* expected_hash,
                            Function c_function) {
  cases.push_back(make_audio_integer_case(
      source, destination, count,
      Variant<AudioConvertFunction>{"c", c_function, IsaRequirement::Scalar}, expected_hash));

  const int src_type = to_avisynth_sample_type(source);
  const int dst_type = to_avisynth_sample_type(destination);
  const auto hwy_c = avs_audio_convert::ResolveHighwayAudioConvert(src_type, dst_type, 0);
  const auto hwy_native = avs_audio_convert::ResolveHighwayAudioConvert(src_type, dst_type, ~0);

  cases.push_back(make_audio_integer_case(
      source, destination, count,
      Variant<AudioConvertFunction>{"hwy_c", hwy_c, IsaRequirement::Scalar}, expected_hash));
  cases.push_back(make_audio_integer_case(
      source, destination, count,
      Variant<AudioConvertFunction>{"hwy_native", hwy_native, IsaRequirement::Scalar}, expected_hash));
}

std::vector<AudioIntegerCase> audio_packed_24_cases() {
  constexpr std::array<std::size_t, 3> counts{15, 16, 17};
  constexpr std::array<const char*, 3> s32_to_s24{"56c34a4a40d2b71f", "ce35878a8c7ba570",
                                                  "27a959747be110f8"};
  constexpr std::array<const char*, 3> s24_to_s32{"544236777f760a57", "1a6fdfcb7259f9d6",
                                                  "4c7abe1834a5cfd4"};
  constexpr std::array<const char*, 3> s24_to_s16{"b9a412f8dd275583", "a5f6058831cbef94",
                                                  "eb36bfad072941c8"};
  constexpr std::array<const char*, 3> s16_to_s24{"9d6f6a2f95056b94", "d615b9752abe3b7f",
                                                  "508d07839f9959a4"};
  constexpr std::array<const char*, 3> s24_to_u8{"be5a748b497f8a72", "fd804383de2f7954",
                                                 "7b8589ad51b4c7bc"};
  constexpr std::array<const char*, 3> u8_to_s24{"14ad13c337e3e265", "3affb442fd5a2bf7",
                                                 "a7b10fbd795a1245"};
  std::vector<AudioIntegerCase> cases;
  for (std::size_t index = 0; index < counts.size(); ++index) {
    const auto count = counts[index];
    add_packed_24_variants(cases, AudioFormat::S32, AudioFormat::S24, count, s32_to_s24[index], convert32To24);
    add_packed_24_variants(cases, AudioFormat::S24, AudioFormat::S32, count, s24_to_s32[index], convert24To32);
    add_packed_24_variants(cases, AudioFormat::S24, AudioFormat::S16, count, s24_to_s16[index], convert24To16);
    add_packed_24_variants(cases, AudioFormat::S16, AudioFormat::S24, count, s16_to_s24[index], convert16To24);
    add_packed_24_variants(cases, AudioFormat::S24, AudioFormat::U8,  count, s24_to_u8[index],  convert24To8);
    add_packed_24_variants(cases, AudioFormat::U8,  AudioFormat::S24, count, u8_to_s24[index],  convert8To24);
  }

  constexpr std::array<std::size_t, 8> edge_counts{0, 1, 2, 3, 31, 32, 33, 64};
  for (const auto count : edge_counts) {
    add_packed_24_variants(cases, AudioFormat::S32, AudioFormat::S24, count, "", convert32To24);
    add_packed_24_variants(cases, AudioFormat::S24, AudioFormat::S32, count, "", convert24To32);
    add_packed_24_variants(cases, AudioFormat::S24, AudioFormat::S16, count, "", convert24To16);
    add_packed_24_variants(cases, AudioFormat::S16, AudioFormat::S24, count, "", convert16To24);
    add_packed_24_variants(cases, AudioFormat::S24, AudioFormat::U8,  count, "", convert24To8);
    add_packed_24_variants(cases, AudioFormat::U8,  AudioFormat::S24, count, "", convert8To24);
  }

  return cases;
}

void add_audio_float_variants(std::vector<AudioFloatCase>& cases, AudioFormat source,
                              AudioFormat destination, std::size_t count, const char* sse_name,
                              IsaRequirement sse_requirement, AudioConvertFunction c_function,
                              AudioConvertFunction sse_function, AudioConvertFunction avx2_function,
                              const char* expected_hash = "") {
  cases.push_back(make_audio_float_case(
      source, destination, count,
      Variant<AudioConvertFunction>{"c", c_function, IsaRequirement::Scalar}, expected_hash));
  if (sse_function != nullptr) {
    cases.push_back(make_audio_float_case(
        source, destination, count,
        Variant<AudioConvertFunction>{sse_name, sse_function, sse_requirement}, expected_hash));
  }
  if (avx2_function != nullptr) {
    cases.push_back(make_audio_float_case(
        source, destination, count,
        Variant<AudioConvertFunction>{"avx2", avx2_function, IsaRequirement::Avx2}, expected_hash));
  }
}

std::vector<AudioFloatCase> audio_float_cases() {
  constexpr std::array<std::size_t, 9> counts{3, 4, 5, 7, 8, 9, 15, 16, 17};
  constexpr std::array<const char*, 9> f32_to_u8{
      "6f15e63fb37c575a", "3a28021824265c2d", "66b1cfc7b7f25862",
      "561e5d7d5e7bf725", "83f0e9d7f6ca57a3", "7df3b53ce739e7f9",
      "70f7678b55537c79", "d26cc00c361369f4", "c52344360f8996c9"};
  constexpr std::array<const char*, 9> f32_to_s16{
      "9fdf2c12fa9cda45", "225b4a1ac27203ae", "75c6b5dfe0f9537b",
      "fa2dbc74fa708fdd", "33e13965e140038a", "0e971aa9e1527ecf",
      "ffcec376b852647e", "853336eb38ef2a2a", "35972615fb81e5e4"};
  constexpr std::array<const char*, 9> f32_to_s32{
      "8025870c1821c9ed", "8fd4e841a1714e07", "ce12e02dabf378d3",
      "46523c3304543fec", "6c949caf27523637", "b35758abba35b489",
      "49ff03bf11065eaf", "d4c900a729fc2049", "17f9f15dbbd30f7f"};
  constexpr std::array<const char*, 9> f32_to_s24{
      "3856194f641f6364", "130bd615dd56e05c", "1ace15baf02dfc76",
      "36b7e16d7e866f28", "29a5e77048f76506", "7d10b3c6122283e0",
      "f31a61d307ad35b6", "fde8884581bbcf19", "36cfd1c0483386f7"};
  std::vector<AudioFloatCase> cases;
  for (std::size_t index = 0; index < counts.size(); ++index) {
    const auto count = counts[index];
    add_audio_float_variants(cases, AudioFormat::U8, AudioFormat::F32, count, "sse4.1",
                             IsaRequirement::Sse41, convert8ToFLT, AUDIO_SIMD_KERNEL(convert8ToFLT_SSE41),
                             AUDIO_SIMD_KERNEL(convert8ToFLT_AVX2));
    add_audio_float_variants(cases, AudioFormat::F32, AudioFormat::U8, count, "sse2",
                             IsaRequirement::Sse2, convertFLTTo8, AUDIO_SIMD_KERNEL(convertFLTTo8_SSE2),
                             AUDIO_SIMD_KERNEL(convertFLTTo8_AVX2), f32_to_u8[index]);
    add_audio_float_variants(cases, AudioFormat::S16, AudioFormat::F32, count, "sse4.1",
                             IsaRequirement::Sse41, convert16ToFLT, AUDIO_SIMD_KERNEL(convert16ToFLT_SSE41),
                             AUDIO_SIMD_KERNEL(convert16ToFLT_AVX2));
    add_audio_float_variants(cases, AudioFormat::F32, AudioFormat::S16, count, "sse2",
                             IsaRequirement::Sse2, convertFLTTo16, AUDIO_SIMD_KERNEL(convertFLTTo16_SSE2),
                             AUDIO_SIMD_KERNEL(convertFLTTo16_AVX2), f32_to_s16[index]);
    add_audio_float_variants(cases, AudioFormat::S32, AudioFormat::F32, count, "sse2",
                             IsaRequirement::Sse2, convert32ToFLT, AUDIO_SIMD_KERNEL(convert32ToFLT_SSE2),
                             AUDIO_SIMD_KERNEL(convert32ToFLT_AVX2));
    add_audio_float_variants(cases, AudioFormat::F32, AudioFormat::S32, count, "sse4.1",
                             IsaRequirement::Sse41, convertFLTTo32, AUDIO_SIMD_KERNEL(convertFLTTo32_SSE41),
                             AUDIO_SIMD_KERNEL(convertFLTTo32_AVX2), f32_to_s32[index]);

    const auto s24_to_f32_native = avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_INT24, SAMPLE_FLOAT, ~0);
    if (s24_to_f32_native) {
      cases.push_back(make_audio_float_case(
          AudioFormat::S24, AudioFormat::F32, count,
          Variant<AudioConvertFunction>{"hwy_native", s24_to_f32_native, IsaRequirement::Scalar}));
    }
    const auto f32_to_s24_native = avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_FLOAT, SAMPLE_INT24, ~0);
    if (f32_to_s24_native) {
      cases.push_back(make_audio_float_case(
          AudioFormat::F32, AudioFormat::S24, count,
          Variant<AudioConvertFunction>{"hwy_native", f32_to_s24_native, IsaRequirement::Scalar},
          f32_to_s24[index]));
    }
  }
  return cases;
}

std::vector<AudioTwoStageCase> audio_two_stage_cases() {
  constexpr std::array<std::size_t, 6> counts{3, 4, 5, 15, 16, 17};
  constexpr std::array<const char*, 6> f32_to_s24_hashes{"3856194f641f6364", "130bd615dd56e05c",
                                                         "1ace15baf02dfc76", "f31a61d307ad35b6",
                                                         "fde8884581bbcf19", "36cfd1c0483386f7"};
  const auto s32_to_s24_hwy = avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_INT32, SAMPLE_INT24, ~0);
  const auto s24_to_s32_hwy = avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_INT24, SAMPLE_INT32, ~0);

  const std::array<Variant<AudioConvertFunction>, 3> float_to_s32{
      Variant<AudioConvertFunction>{"c", convertFLTTo32, IsaRequirement::Scalar},
      Variant<AudioConvertFunction>{"sse4.1", AUDIO_SIMD_KERNEL(convertFLTTo32_SSE41), IsaRequirement::Sse41},
      Variant<AudioConvertFunction>{"avx2", AUDIO_SIMD_KERNEL(convertFLTTo32_AVX2), IsaRequirement::Avx2}};
  const std::array<Variant<AudioConvertFunction>, 2> s32_to_s24{
      Variant<AudioConvertFunction>{"c", convert32To24, IsaRequirement::Scalar},
      Variant<AudioConvertFunction>{"hwy_native", s32_to_s24_hwy, IsaRequirement::Scalar}};
  const std::array<Variant<AudioConvertFunction>, 2> s24_to_s32{
      Variant<AudioConvertFunction>{"c", convert24To32, IsaRequirement::Scalar},
      Variant<AudioConvertFunction>{"hwy_native", s24_to_s32_hwy, IsaRequirement::Scalar}};
  const std::array<Variant<AudioConvertFunction>, 3> s32_to_float{
      Variant<AudioConvertFunction>{"c", convert32ToFLT, IsaRequirement::Scalar},
      Variant<AudioConvertFunction>{"sse2", AUDIO_SIMD_KERNEL(convert32ToFLT_SSE2), IsaRequirement::Sse2},
      Variant<AudioConvertFunction>{"avx2", AUDIO_SIMD_KERNEL(convert32ToFLT_AVX2), IsaRequirement::Avx2}};

  std::vector<AudioTwoStageCase> cases;
  for (std::size_t index = 0; index < counts.size(); ++index) {
    const auto count = counts[index];
    for (const auto& first : float_to_s32) {
      for (const auto& second : s32_to_s24) {
        if (!first.function || !second.function) continue;
        cases.push_back(make_audio_two_stage_case(AudioFormat::F32, AudioFormat::S24, count, first,
                                                  second, f32_to_s24_hashes[index]));
      }
    }
    for (const auto& first : s24_to_s32) {
      for (const auto& second : s32_to_float) {
        if (!first.function || !second.function) continue;
        cases.push_back(
            make_audio_two_stage_case(AudioFormat::S24, AudioFormat::F32, count, first, second));
      }
    }
  }
  return cases;
}

#undef AUDIO_SIMD_KERNEL

class AudioIntegerKernels : public ::testing::TestWithParam<AudioIntegerCase> {};

TEST_P(AudioIntegerKernels, MatchesIndependentIntegerReference) {
  const auto& test_case = GetParam();
  ASSERT_NE(test_case.variant.function, nullptr);
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_audio_integer_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, AudioIntegerKernels, ::testing::ValuesIn(audio_integer_cases()),
                         [](const ::testing::TestParamInfo<AudioIntegerCase>& info) {
                           return info.param.name;
                         });

class AudioPacked24Kernels : public ::testing::TestWithParam<AudioIntegerCase> {};

TEST_P(AudioPacked24Kernels, MatchesIndependentPacked24Reference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_audio_integer_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Packed24, AudioPacked24Kernels,
                         ::testing::ValuesIn(audio_packed_24_cases()),
                         [](const ::testing::TestParamInfo<AudioIntegerCase>& info) {
                           return info.param.name;
                         });

class AudioFloatKernels : public ::testing::TestWithParam<AudioFloatCase> {};

TEST_P(AudioFloatKernels, MatchesIndependentFloatConversionReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_audio_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Float, AudioFloatKernels, ::testing::ValuesIn(audio_float_cases()),
                         [](const ::testing::TestParamInfo<AudioFloatCase>& info) {
                           return info.param.name;
                         });

class AudioTwoStageKernels : public ::testing::TestWithParam<AudioTwoStageCase> {};

TEST_P(AudioTwoStageKernels, MatchesConvertAudioTwoStageReference) {
  const auto& test_case = GetParam();
  const auto features = CpuFeatures::detect();
  if (!variant_supported(test_case.first_variant, features)) {
    GTEST_SKIP() << "host does not support " << test_case.first_variant.name;
  }
  if (!variant_supported(test_case.second_variant, features)) {
    GTEST_SKIP() << "host does not support " << test_case.second_variant.name;
  }
  run_audio_two_stage_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(TwoStage, AudioTwoStageKernels,
                         ::testing::ValuesIn(audio_two_stage_cases()),
                         [](const ::testing::TestParamInfo<AudioTwoStageCase>& info) {
                           return info.param.name;
                         });


TEST(AudioPacked24Float, MatchesComposedCAtQuantizationBoundaries) {
  const auto convert = avs_audio_convert::ResolveHighwayAudioConvert(SAMPLE_FLOAT, SAMPLE_INT24, ~0);
  if (!convert) GTEST_SKIP() << "No SIMD target on this host";
  const float inf = std::numeric_limits<float>::infinity();
  std::vector<float> values{0.0f, -0.0f, inf, -inf,
      std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min()};
  for (float boundary : {-1.0f, -0x1p-23f, -0x1p-31f, 0x1p-31f, 0x1p-23f, 1.0f}) {
    values.push_back(std::nextafter(boundary, -inf));
    values.push_back(boundary);
    values.push_back(std::nextafter(boundary, inf));
  }
  const auto seeds = values;
  while (values.size() < 65) values.push_back(seeds[values.size() % seeds.size()]);
  // Rotate each value through vector blocks and the scalar tail.
  for (size_t rotation = 0; rotation < values.size(); ++rotation) {
    std::rotate(values.begin(), values.begin() + 1, values.end());
    std::vector<int32_t> intermediate(values.size());
    std::vector<uint8_t> expected(values.size() * 3), actual(expected.size());
    convertFLTTo32(values.data(), intermediate.data(), int(values.size()));
    convert32To24(intermediate.data(), expected.data(), int(values.size()));
    convert(values.data(), actual.data(), int(values.size()));
    EXPECT_EQ(actual, expected) << "rotation " << rotation;
  }
}

#ifdef _WIN32
TEST(AudioPacked24Boundary, NoAccessPastInputOrOutputBufferEnd) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const size_t page_size = si.dwPageSize;
  const auto release = [](uint8_t* p) { if (p) VirtualFree(p, 0, MEM_RELEASE); };
  using Allocation = std::unique_ptr<uint8_t, decltype(release)>;
  Allocation input(static_cast<uint8_t*>(VirtualAlloc(nullptr, page_size * 2,
      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)), release);
  Allocation output(static_cast<uint8_t*>(VirtualAlloc(nullptr, page_size * 2,
      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)), release);
  ASSERT_NE(input, nullptr);
  ASSERT_NE(output, nullptr);
  DWORD old_protect;
  ASSERT_TRUE(VirtualProtect(input.get() + page_size, page_size, PAGE_NOACCESS, &old_protect));
  ASSERT_TRUE(VirtualProtect(output.get() + page_size, page_size, PAGE_NOACCESS, &old_protect));
  for (int format : {SAMPLE_INT8, SAMPLE_INT16, SAMPLE_INT32, SAMPLE_FLOAT}) {
    const size_t width = format == SAMPLE_INT8 ? 1 : format == SAMPLE_INT16 ? 2 : 4;
    for (bool pack : {false, true}) {
      const auto convert = avs_audio_convert::ResolveHighwayAudioConvert(
          pack ? format : SAMPLE_INT24, pack ? SAMPLE_INT24 : format, ~0);
      if (format == SAMPLE_FLOAT && avs_audio_convert::GetHighwayAudioConvertChosenTarget(~0) == 0) {
        EXPECT_EQ(convert, nullptr);
        continue;
      }
      ASSERT_NE(convert, nullptr);
      for (size_t count : {0, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65}) {
        SCOPED_TRACE(::testing::Message() << format << " pack=" << pack << " count=" << count);
        auto* in = input.get() + page_size - count * (pack ? width : 3);
        auto* out = output.get() + page_size - count * (pack ? 3 : width);
        std::memset(in, 0x5A, count * (pack ? width : 3));
        convert(in, out, int(count));
      }
    }
  }
}
#endif


}  // namespace
}  // namespace avsut::test
