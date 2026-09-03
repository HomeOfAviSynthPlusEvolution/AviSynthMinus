#include <gtest/gtest.h>
#include <avisynth.h>
#include "core/InternalEnvironment.h"
#include "core/cache.h"
#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

namespace avsut::test {
namespace {

TEST(CacheClass, CacheInstantiationWorks) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{64, 64, VideoInfo::CS_BGR32, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(vi);
  auto* clip_impl = new StaticFrameClip(vi, frame);
  const PClip source(clip_impl);

  std::mutex cache_guard_mutex;
  auto* internal_env = static_cast<InternalEnvironment*>(static_cast<IScriptEnvironment*>(environment.get()));
  PClip cache_clip = new AvsCache(source, nullptr, cache_guard_mutex, internal_env);
  EXPECT_NE(cache_clip, nullptr);
  PVideoFrame cached_frame = cache_clip->GetFrame(0, environment.get());
  EXPECT_NE(cached_frame, nullptr);
}

}  // namespace
}  // namespace avsut::test
