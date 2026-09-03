#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B9_UNDEF_AVS_UNUSED
#endif
#include "core/parser/script.h"
#include "filters/conditional/conditional.h"
#include "filters/conditional/conditional_functions.h"
#include "filters/conditional/conditional_reader.h"
#ifdef AVSUT_FINDING_B9_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B9_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace avsut::test {
namespace {

struct VideoSequence {
  VideoInfo video_info;
  std::vector<PVideoFrame> frames;
  FrameSequenceClip* clip_impl;
  PClip clip;
  std::vector<FrameSnapshot> snapshots;
};

VideoSequence make_y8_sequence(AviSynthEnvironment& environment,
                               std::initializer_list<std::uint8_t> luma_values) {
  if (luma_values.size() == 0 ||
      luma_values.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("B9 Y8 sequence requires a representable nonzero frame count");
  }

  const auto video_info = make_video_info(
      VideoInfoSpec{4, 2, VideoInfo::CS_Y8, static_cast<int>(luma_values.size()), 25, 1});
  std::vector<PVideoFrame> frames;
  std::vector<FrameSnapshot> snapshots;
  frames.reserve(luma_values.size());
  snapshots.reserve(luma_values.size());
  for (const auto luma : luma_values) {
    PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
    fill_plane_full_pitch(frame, luma, PLANAR_Y);
    snapshots.push_back(FrameSnapshot::capture(frame, video_info));
    frames.push_back(frame);
  }

  auto* clip_impl = new FrameSequenceClip(video_info, frames);
  return VideoSequence{video_info, std::move(frames), clip_impl, PClip(clip_impl),
                       std::move(snapshots)};
}

std::uint8_t first_luma(const PVideoFrame& frame) { return frame->GetReadPtr(PLANAR_Y)[0]; }

void expect_source_pixels_unchanged(const VideoSequence& source, const char* operation) {
  ASSERT_EQ(source.frames.size(), source.snapshots.size());
  for (std::size_t index = 0; index < source.frames.size(); ++index) {
    EXPECT_EQ(FrameSnapshot::capture(source.frames[index], source.video_info),
              source.snapshots[index])
        << "B9 " << operation << " modified source frame=" << index;
  }
}

void set_current_frame(AviSynthEnvironment& environment, int frame_number) {
  environment.get()->SetVar("current_frame", AVSValue(frame_number));
}

AVSValue script_generated_nan(IScriptEnvironment* environment) {
  const AVSValue negative_one(-1.0F);
  return Sqrt(AVSValue(&negative_one, 1), nullptr, environment);
}

class TemporaryTextFile {
 public:
  explicit TemporaryTextFile(const char* contents)
      : path_(std::filesystem::temp_directory_path() /
              ("avsut-b9-" + std::to_string(next_id()) + ".txt")),
        path_string_(path_.string()) {
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    if (!stream) {
      throw std::runtime_error("B9 could not create temporary ConditionalReader input");
    }
    stream << contents;
    if (!stream) {
      throw std::runtime_error("B9 could not write temporary ConditionalReader input");
    }
  }

  ~TemporaryTextFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  TemporaryTextFile(const TemporaryTextFile&) = delete;
  TemporaryTextFile& operator=(const TemporaryTextFile&) = delete;

  const char* c_str() const noexcept { return path_string_.c_str(); }

 private:
  static std::uint64_t next_id() {
    static std::atomic<std::uint64_t> counter{0};
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<std::uint64_t>(now) + counter.fetch_add(1, std::memory_order_relaxed);
  }

  std::filesystem::path path_;
  std::string path_string_;
};

TEST(ConditionalFilterLength, RepeatsShorterFalseSourceAtItsLastFrame) {
  AviSynthEnvironment environment;
  const VideoSequence source1 = make_y8_sequence(environment, {11, 12, 13});
  const VideoSequence source2 = make_y8_sequence(environment, {91});
  ConditionalFilter filter(source1.clip, source1.clip, source2.clip, "0", ">", "0", false, false,
                           environment.get());

  PVideoFrame output;
  ASSERT_NO_THROW(output = filter.GetFrame(2, environment.get()))
      << "B9 ConditionalFilter false branch must clamp the selected source independently";
  ASSERT_NE(output, nullptr);
  EXPECT_EQ(first_luma(output), 91);
  EXPECT_TRUE(output->CheckMemory() != 1);
  EXPECT_TRUE(source1.clip_impl->frame_requests().empty());
  EXPECT_EQ(source2.clip_impl->frame_requests(), std::vector<int>({0}));
  expect_source_pixels_unchanged(source1, "ConditionalFilter false tail");
  expect_source_pixels_unchanged(source2, "ConditionalFilter false tail");
}

TEST(ConditionalFilterLength, UsesLastAvailableSourceForComparisonErrorPicture) {
  AviSynthEnvironment environment;
  const VideoSequence source1 = make_y8_sequence(environment, {31});
  const VideoSequence source2 = make_y8_sequence(environment, {71, 72, 73});
  ConditionalFilter filter(source1.clip, source1.clip, source2.clip, "0.5", "=", "true", false,
                           false, environment.get());

  PVideoFrame output;
  ASSERT_NO_THROW(output = filter.GetFrame(2, environment.get()))
      << "B9 ConditionalFilter error-picture path must clamp source1 at the output tail";
  ASSERT_NE(output, nullptr);
  EXPECT_TRUE(output->CheckMemory() != 1);
  EXPECT_EQ(source1.clip_impl->frame_requests(), std::vector<int>({0}));
  EXPECT_TRUE(source2.clip_impl->frame_requests().empty());
  expect_source_pixels_unchanged(source1, "ConditionalFilter comparison error tail");
  expect_source_pixels_unchanged(source2, "ConditionalFilter comparison error tail");
}

TEST(ConditionalFilterComparison, PreservesDoublePrecisionForStrictOrdering) {
  AviSynthEnvironment environment;
  const VideoSequence source1 = make_y8_sequence(environment, {21});
  const VideoSequence source2 = make_y8_sequence(environment, {81});
  constexpr double kSmaller = 1.0;
  constexpr double kLarger = 1.0 + 1.0e-8;
  static_assert(kLarger > kSmaller);
  environment.get()->SetVar("B9LargerDouble", AVSValue(kLarger));
  environment.get()->SetVar("B9SmallerDouble", AVSValue(kSmaller));
  ConditionalFilter filter(source1.clip, source1.clip, source2.clip, "B9LargerDouble", ">",
                           "B9SmallerDouble", false, false, environment.get());

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_NE(output, nullptr);
  EXPECT_EQ(first_luma(output), 21);
  EXPECT_TRUE(output->CheckMemory() != 1);
  EXPECT_EQ(source1.clip_impl->frame_requests(), std::vector<int>({0}));
  EXPECT_TRUE(source2.clip_impl->frame_requests().empty());
  expect_source_pixels_unchanged(source1, "ConditionalFilter double comparison");
  expect_source_pixels_unchanged(source2, "ConditionalFilter double comparison");
}

TEST(ComparePlaneLength, ClampsSecondClipIndependentlyAtOutputTail) {
  AviSynthEnvironment environment;
  const VideoSequence first = make_y8_sequence(environment, {10, 20, 50});
  const VideoSequence second = make_y8_sequence(environment, {10});
  set_current_frame(environment, 2);

  AVSValue result;
  ASSERT_NO_THROW(result = ComparePlane::CmpPlane(AVSValue(first.clip), AVSValue(second.clip),
                                                  nullptr, PLANAR_Y, environment.get()))
      << "B9 Difference must not request a tail frame outside its second clip";
  EXPECT_DOUBLE_EQ(result.AsFloat(), 40.0);
  EXPECT_EQ(first.clip_impl->frame_requests(), std::vector<int>({2}));
  EXPECT_EQ(second.clip_impl->frame_requests(), std::vector<int>({0}));
  expect_source_pixels_unchanged(first, "ComparePlane tail");
  expect_source_pixels_unchanged(second, "ComparePlane tail");
}

TEST(CopyPropertiesLength, RepeatsShorterPropertySourceAtOutputTail) {
  AviSynthEnvironment environment;
  const VideoSequence target = make_y8_sequence(environment, {41, 42, 43});
  VideoSequence property_source = make_y8_sequence(environment, {61});
  AVSMap* properties = environment.get()->getFramePropsRW(property_source.frames.front());
  ASSERT_NE(properties, nullptr);
  ASSERT_EQ(environment.get()->propSetInt(properties, "B9TailProperty", 2468,
                                          AVSPropAppendMode::PROPAPPENDMODE_REPLACE),
            0);

  CopyProperties filter(target.clip, property_source.clip, false, AVSValue(), false,
                        environment.get());
  PVideoFrame output;
  ASSERT_NO_THROW(output = filter.GetFrame(2, environment.get()))
      << "B9 propCopy must not request a tail frame outside its property source";
  ASSERT_NE(output, nullptr);
  EXPECT_TRUE(output->CheckMemory() != 1);
  int error = 0;
  const AVSMap* output_properties = environment.get()->getFramePropsRO(output);
  ASSERT_NE(output_properties, nullptr);
  EXPECT_EQ(environment.get()->propGetInt(output_properties, "B9TailProperty", 0, &error), 2468);
  EXPECT_EQ(error, 0);
  EXPECT_EQ(target.clip_impl->frame_requests(), std::vector<int>({2}));
  EXPECT_EQ(property_source.clip_impl->frame_requests(), std::vector<int>({0}));
  expect_source_pixels_unchanged(target, "propCopy tail");
  expect_source_pixels_unchanged(property_source, "propCopy tail");
}

TEST(MinMaxThreshold, RejectsScriptGeneratedNonFiniteThreshold) {
  AviSynthEnvironment environment;
  const VideoSequence source = make_y8_sequence(environment, {72});
  const AVSValue nan = script_generated_nan(environment.get());
  ASSERT_TRUE(nan.IsFloat());
  ASSERT_TRUE(std::isnan(nan.AsFloat()));
  set_current_frame(environment, 0);
  const AVSValue args[] = {source.clip, nan, 0};

  EXPECT_THROW(
      {
        (void)MinMaxPlane::Create_max(AVSValue(args, static_cast<int>(std::size(args))),
                                      reinterpret_cast<void*>(static_cast<std::intptr_t>(PLANAR_Y)),
                                      environment.get());
      },
      AvisynthError)
      << "B9 MinMax threshold=NaN must be rejected before histogram quantization";
  expect_source_pixels_unchanged(source, "MinMax NaN threshold");
}

TEST(GetAllProperties, PreservesEveryUtf8DataArrayElement) {
  AviSynthEnvironment environment;
  VideoSequence source = make_y8_sequence(environment, {72});
  constexpr char kPropertyName[] = "B9StringArray";
  AVSMap* properties = environment.get()->getFramePropsRW(source.frames.front());
  ASSERT_NE(properties, nullptr);
  ASSERT_EQ(environment.get()->propSetDataH(properties, kPropertyName, "first", -1,
                                            AVSPropDataTypeHint::PROPDATATYPEHINT_UTF8,
                                            AVSPropAppendMode::PROPAPPENDMODE_REPLACE),
            0);
  ASSERT_EQ(environment.get()->propSetDataH(properties, kPropertyName, "second", -1,
                                            AVSPropDataTypeHint::PROPDATATYPEHINT_UTF8,
                                            AVSPropAppendMode::PROPAPPENDMODE_APPEND),
            0);
  const AVSValue args[] = {source.clip, 0};

  const AVSValue all_properties = GetAllProperties::Create(
      AVSValue(args, static_cast<int>(std::size(args))), nullptr, environment.get());
  const AVSValue* values = nullptr;
  for (int index = 0; index < all_properties.ArraySize(); ++index) {
    const AVSValue& pair = all_properties[index];
    if (pair.ArraySize() == 2 && pair[0].IsString() &&
        std::string(pair[0].AsString()) == kPropertyName) {
      values = &pair[1];
      break;
    }
  }

  ASSERT_NE(values, nullptr) << "B9 propGetAll did not return the test property";
  ASSERT_TRUE(values->IsArray()) << "B9 propGetAll collapsed a multi-element UTF-8 property";
  ASSERT_EQ(values->ArraySize(), 2);
  ASSERT_TRUE((*values)[0].IsString());
  ASSERT_TRUE((*values)[1].IsString());
  EXPECT_STREQ((*values)[0].AsString(), "first");
  EXPECT_STREQ((*values)[1].AsString(), "second");
  EXPECT_EQ(source.clip_impl->frame_requests(), std::vector<int>({0}));
  expect_source_pixels_unchanged(source, "propGetAll UTF-8 array");
}

TEST(ConditionalReaderInterpolation, RejectsOrDefinesSingleFrameInterpolation) {
  AviSynthEnvironment environment;
  const VideoSequence source = make_y8_sequence(environment, {10, 20, 30});
  const TemporaryTextFile input("Type int\nInterpolate 1 1 10 20\n");

  try {
    ConditionalReader filter(source.clip, input.c_str(), "B9SingleFrame", false, "", false,
                             environment.get());
    const PVideoFrame output = filter.GetFrame(1, environment.get());
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(output->CheckMemory() != 1);
    const AVSValue value = environment.get()->GetVarDef("B9SingleFrame");
    ASSERT_TRUE(value.IsInt());
    EXPECT_TRUE(value.AsInt() == 10 || value.AsInt() == 20)
        << "B9 ConditionalReader single-frame interpolation produced " << value.AsInt();
    EXPECT_EQ(source.clip_impl->frame_requests(), std::vector<int>({1}));
  } catch (const AvisynthError&) {
    SUCCEED() << "B9 ConditionalReader rejected a zero-length interpolation";
  }
  expect_source_pixels_unchanged(source, "ConditionalReader single-frame interpolation");
}

TEST(ShowPropertiesFactory, RejectsScriptGeneratedNonFiniteCoordinates) {
  AviSynthEnvironment environment;
  const VideoSequence source = make_y8_sequence(environment, {72});
  const AVSValue nan = script_generated_nan(environment.get());
  ASSERT_TRUE(nan.IsFloat());
  ASSERT_TRUE(std::isnan(nan.AsFloat()));
  std::array<AVSValue, 11> args{};
  args[0] = source.clip;
  args[7] = nan;
  args[8] = 0;
  args[9] = 7;

  EXPECT_THROW(
      {
        (void)ShowProperties::Create(AVSValue(args.data(), static_cast<int>(args.size())), nullptr,
                                     environment.get());
      },
      AvisynthError)
      << "B9 propShow x=NaN must be rejected before conversion to integer coordinates";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty());
  expect_source_pixels_unchanged(source, "propShow NaN coordinate");
}

}  // namespace
}  // namespace avsut::test
