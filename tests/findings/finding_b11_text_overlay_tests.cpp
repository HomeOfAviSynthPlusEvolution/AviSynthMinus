#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B11_TEXT_OVERLAY_UNDEF_AVS_UNUSED
#endif
#include "filters/text-overlay.h"
#ifdef AVSUT_FINDING_B11_TEXT_OVERLAY_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B11_TEXT_OVERLAY_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace avsut::test {
namespace {

class TemporaryCompareLog {
 public:
  TemporaryCompareLog()
      : path_(std::filesystem::temp_directory_path() /
              ("avsut-b11-compare-" + std::to_string(next_id()) + ".log")),
        path_string_(path_.string()) {}

  ~TemporaryCompareLog() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  TemporaryCompareLog(const TemporaryCompareLog&) = delete;
  TemporaryCompareLog& operator=(const TemporaryCompareLog&) = delete;

  const char* c_str() const noexcept { return path_string_.c_str(); }

  std::string contents() const {
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) {
      throw std::runtime_error("B11 could not read Compare logfile");
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  }

 private:
  static std::uint64_t next_id() {
    static std::atomic<std::uint64_t> counter{0};
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<std::uint64_t>(now) + counter.fetch_add(1, std::memory_order_relaxed);
  }

  std::filesystem::path path_;
  std::string path_string_;
};

struct CompareMetrics {
  double mean_absolute_deviation{};
  double mean_deviation{};
  double psnr{};
};

CompareMetrics first_frame_metrics(const TemporaryCompareLog& log) {
  std::istringstream lines(log.contents());
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    unsigned frame_number = 0;
    CompareMetrics metrics;
    int positive_deviation = 0;
    int negative_deviation = 0;
    if (fields >> frame_number >> metrics.mean_absolute_deviation >> metrics.mean_deviation >>
        positive_deviation >> negative_deviation >> metrics.psnr) {
      return metrics;
    }
  }
  throw std::runtime_error("B11 Compare logfile did not contain a frame metric row");
}

struct PackedSource {
  VideoInfo video_info;
  PVideoFrame frame;
  FrameSequenceClip* clip_impl;
  PClip clip;
};

PackedSource make_packed_source(AviSynthEnvironment& environment, int pixel_type, int width,
                                int height) {
  const VideoInfo video_info = make_video_info(VideoInfoSpec{width, height, pixel_type, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0, DEFAULT_PLANE);
  return PackedSource{video_info, frame, nullptr, PClip()};
}

void seal_packed_source(PackedSource& source) {
  source.clip_impl =
      new FrameSequenceClip(source.video_info, std::vector<PVideoFrame>{source.frame});
  source.clip = PClip(source.clip_impl);
}

void set_rgb48_components(PackedSource& source, std::uint16_t blue, std::uint16_t green,
                          std::uint16_t red) {
  const int pitch = source.frame->GetPitch() / static_cast<int>(sizeof(std::uint16_t));
  const int height = source.frame->GetHeight();
  auto* pixels = reinterpret_cast<std::uint16_t*>(source.frame->GetWritePtr());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < source.video_info.width; ++x) {
      pixels[y * pitch + x * 3] = blue;
      pixels[y * pitch + x * 3 + 1] = green;
      pixels[y * pitch + x * 3 + 2] = red;
    }
  }
}

void set_rgb32_components(PackedSource& source, std::uint8_t blue, std::uint8_t green,
                          std::uint8_t red) {
  const int pitch = source.frame->GetPitch();
  const int height = source.frame->GetHeight();
  auto* pixels = source.frame->GetWritePtr();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < source.video_info.width; ++x) {
      pixels[y * pitch + x * 4] = blue;
      pixels[y * pitch + x * 4 + 1] = green;
      pixels[y * pitch + x * 4 + 2] = red;
      pixels[y * pitch + x * 4 + 3] = 0;
    }
  }
}

void expect_source_unchanged(const PackedSource& source, const FrameSnapshot& before,
                             const char* operation) {
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), before)
      << "B11 " << operation << " modified its source";
}

class PlacementCompare {
 public:
  PlacementCompare(void* storage, PClip first, PClip second, const char* channels,
                   const char* logfile, IScriptEnvironment* environment)
      : filter_(::new (storage)
                    Compare(first, second, channels, logfile, false, environment)) {}

  ~PlacementCompare() { filter_->~Compare(); }

  PlacementCompare(const PlacementCompare&) = delete;
  PlacementCompare& operator=(const PlacementCompare&) = delete;

  PVideoFrame get_frame(int frame_number, IScriptEnvironment* environment) {
    return filter_->GetFrame(frame_number, environment);
  }

 private:
  Compare* filter_;
};

TEST(CompareRgb48, NormalizesMetricsBySelectedComponentCount) {
  AviSynthEnvironment environment;
  PackedSource first = make_packed_source(environment, VideoInfo::CS_BGR48, 4, 1);
  PackedSource second = make_packed_source(environment, VideoInfo::CS_BGR48, 4, 1);
  set_rgb48_components(first, 0, 0, 0);
  set_rgb48_components(second, 1, 1, 1);
  seal_packed_source(first);
  seal_packed_source(second);
  const FrameSnapshot first_before = FrameSnapshot::capture(first.frame, first.video_info);
  const FrameSnapshot second_before = FrameSnapshot::capture(second.frame, second.video_info);
  TemporaryCompareLog log;

  PVideoFrame output;
  {
    Compare filter(first.clip, second.clip, "RGB", log.c_str(), false, environment.get());
    ASSERT_NO_THROW(output = filter.GetFrame(0, environment.get()));
    ASSERT_NE(output, nullptr);
    EXPECT_NE(output->CheckMemory(), 1);
  }

  const CompareMetrics metrics = first_frame_metrics(log);
  EXPECT_NEAR(metrics.mean_absolute_deviation, 1.0, 0.00005)
      << "B11 RGB48 Compare must normalize by all selected 16-bit components";
  EXPECT_NEAR(metrics.mean_deviation, -1.0, 0.00005)
      << "B11 RGB48 Compare must normalize signed deviation by all selected components";
  expect_source_unchanged(first, first_before, "Compare RGB48 normalization first");
  expect_source_unchanged(second, second_before, "Compare RGB48 normalization second");
}

TEST(CompareRgb48, IgnoresUnselectedChannelsAfterStorageReuse) {
  AviSynthEnvironment environment;
  PackedSource first = make_packed_source(environment, VideoInfo::CS_BGR48, 4, 1);
  PackedSource second = make_packed_source(environment, VideoInfo::CS_BGR48, 4, 1);
  set_rgb48_components(first, 0, 0, 37);
  set_rgb48_components(second, 500, 0, 37);
  seal_packed_source(first);
  seal_packed_source(second);
  const FrameSnapshot first_before = FrameSnapshot::capture(first.frame, first.video_info);
  const FrameSnapshot second_before = FrameSnapshot::capture(second.frame, second.video_info);
  TemporaryCompareLog priming_log;
  TemporaryCompareLog selected_log;
  alignas(Compare) unsigned char storage[sizeof(Compare)];

  {
    PlacementCompare priming(storage, first.clip, second.clip, "RGB", priming_log.c_str(),
                             environment.get());
    const PVideoFrame output = priming.get_frame(0, environment.get());
    ASSERT_NE(output, nullptr);
    EXPECT_NE(output->CheckMemory(), 1);
  }
  {
    PlacementCompare selected(storage, first.clip, second.clip, "R", selected_log.c_str(),
                              environment.get());
    const PVideoFrame output = selected.get_frame(0, environment.get());
    ASSERT_NE(output, nullptr);
    EXPECT_NE(output->CheckMemory(), 1);
  }

  const CompareMetrics metrics = first_frame_metrics(selected_log);
  EXPECT_NEAR(metrics.mean_absolute_deviation, 0.0, 0.00005)
      << "B11 RGB48 Compare must ignore differing unselected B samples";
  EXPECT_NEAR(metrics.mean_deviation, 0.0, 0.00005)
      << "B11 RGB48 Compare must ignore differing unselected B samples";
  expect_source_unchanged(first, first_before, "Compare RGB48 selected-channel first");
  expect_source_unchanged(second, second_before, "Compare RGB48 selected-channel second");
}

TEST(CompareRgb32, ComputesCorrectMetricsFor4kMaximumDifference) {
  AviSynthEnvironment environment;
  PackedSource first = make_packed_source(environment, VideoInfo::CS_BGR32, 3840, 2160);
  PackedSource second = make_packed_source(environment, VideoInfo::CS_BGR32, 3840, 2160);
  set_rgb32_components(first, 0, 0, 0);
  set_rgb32_components(second, 255, 255, 255);
  seal_packed_source(first);
  seal_packed_source(second);
  TemporaryCompareLog log;

  PVideoFrame output;
  {
    Compare filter(first.clip, second.clip, "RGB", log.c_str(), false, environment.get());
    ASSERT_NO_THROW(output = filter.GetFrame(0, environment.get()));
    ASSERT_NE(output, nullptr);
    EXPECT_NE(output->CheckMemory(), 1);
    EXPECT_NE(second.frame->CheckMemory(), 1);
  }

  const CompareMetrics metrics = first_frame_metrics(log);
  EXPECT_NEAR(metrics.mean_absolute_deviation, 255.0, 0.00005)
      << "B11 RGB32 Compare 4K maximum difference";
  EXPECT_NEAR(metrics.mean_deviation, -255.0, 0.00005) << "B11 RGB32 Compare 4K maximum difference";
}

TEST(CompareConstruction, RejectsSecondClipShorterThanFirst) {
  AviSynthEnvironment environment;
  const VideoInfo video_info = make_video_info(VideoInfoSpec{8, 8, VideoInfo::CS_Y8, 2, 25, 1});
  PVideoFrame first_frame0 = environment.get()->NewVideoFrame(video_info);
  PVideoFrame first_frame1 = environment.get()->NewVideoFrame(video_info);
  PVideoFrame second_frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(first_frame0, 0x10, PLANAR_Y);
  fill_plane_full_pitch(first_frame1, 0x20, PLANAR_Y);
  fill_plane_full_pitch(second_frame, 0x30, PLANAR_Y);
  const FrameSnapshot first_frame0_before = FrameSnapshot::capture(first_frame0, video_info);
  const FrameSnapshot first_frame1_before = FrameSnapshot::capture(first_frame1, video_info);
  const FrameSnapshot second_frame_before = FrameSnapshot::capture(second_frame, video_info);
  auto* first_impl =
      new FrameSequenceClip(video_info, std::vector<PVideoFrame>{first_frame0, first_frame1});
  const PClip first(first_impl);
  VideoInfo shorter_info = video_info;
  shorter_info.num_frames = 1;
  auto* second_impl = new FrameSequenceClip(shorter_info, std::vector<PVideoFrame>{second_frame});
  const PClip second(second_impl);

  EXPECT_THROW(
      { Compare filter(first, second, "Y", "", false, environment.get()); }, AvisynthError)
      << "B11 Compare first_frames=" << video_info.num_frames
      << " second_frames=" << shorter_info.num_frames;
  EXPECT_TRUE(first_impl->frame_requests().empty())
      << "B11 Compare requested first frames during construction";
  EXPECT_TRUE(second_impl->frame_requests().empty())
      << "B11 Compare requested second frames during construction";
  EXPECT_EQ(FrameSnapshot::capture(first_frame0, video_info), first_frame0_before);
  EXPECT_EQ(FrameSnapshot::capture(first_frame1, video_info), first_frame1_before);
  EXPECT_EQ(FrameSnapshot::capture(second_frame, shorter_info), second_frame_before);
}

TEST(CompareLogfile, AvoidsNonFiniteSummaryWithoutFrameEvaluation) {
  AviSynthEnvironment environment;
  PackedSource first = make_packed_source(environment, VideoInfo::CS_BGR32, 4, 4);
  PackedSource second = make_packed_source(environment, VideoInfo::CS_BGR32, 4, 4);
  seal_packed_source(first);
  seal_packed_source(second);
  const FrameSnapshot first_before = FrameSnapshot::capture(first.frame, first.video_info);
  const FrameSnapshot second_before = FrameSnapshot::capture(second.frame, second.video_info);
  TemporaryCompareLog log;

  {
    Compare filter(first.clip, second.clip, "RGB", log.c_str(), false, environment.get());
  }

  std::string summary = log.contents();
  std::transform(summary.begin(), summary.end(), summary.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  EXPECT_NE(summary.find("total frames processed: 0"), std::string::npos);
  EXPECT_EQ(summary.find("nan"), std::string::npos)
      << "B11 Compare logfile must not summarize unprocessed frames as NaN";
  EXPECT_EQ(summary.find("inf"), std::string::npos)
      << "B11 Compare logfile must not summarize unprocessed frames as infinity";
  EXPECT_TRUE(first.clip_impl->frame_requests().empty());
  EXPECT_TRUE(second.clip_impl->frame_requests().empty());
  expect_source_unchanged(first, first_before, "Compare logfile first");
  expect_source_unchanged(second, second_before, "Compare logfile second");
}

}  // namespace
}  // namespace avsut::test
