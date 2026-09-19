#include "core/internal.h"
#include "core/AVSMap.h"
#include "core/InternalEnvironment.h"
#include "core/cache.h"
#include "support/avisynth_environment.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <new>
#include <mutex>

namespace {
thread_local bool forbid_allocation = false;
}

// Isolated executable: inject failure only around the property-drain primitive.
void* operator new(std::size_t size) {
  if (forbid_allocation) throw std::bad_alloc();
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  try { return ::operator new(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t& tag) noexcept { return ::operator new(n, tag); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }

namespace avsut::test {
namespace {
VideoInfo frame_info(int width = 64, int height = 64) {
  VideoInfo vi{};
  vi.width = width; vi.height = height; vi.pixel_type = VideoInfo::CS_Y8;
  vi.num_frames = 1; vi.fps_numerator = vi.fps_denominator = 1;
  return vi;
}

class DestructionMarker : public IClip {
public:
  explicit DestructionMarker(int& count) : count_(count) {}
  ~DestructionMarker() override { ++count_; }
  const VideoInfo& __stdcall GetVideoInfo() override { return vi_; }
  PVideoFrame __stdcall GetFrame(int, IScriptEnvironment* env) override { return env->NewVideoFrame(vi_); }
  void __stdcall GetAudio(void*, int64_t, int64_t, IScriptEnvironment*) override {}
  bool __stdcall GetParity(int) override { return false; }
  int __stdcall SetCacheHints(int hint, int) override { return hint == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0; }
private:
  int& count_;
  VideoInfo vi_ = frame_info();
};

void mark(IScriptEnvironment* env, PVideoFrame& frame, int& destroyed) {
  PClip marker = new DestructionMarker(destroyed);
  ASSERT_EQ(env->propSetClip(env->getFramePropsRW(frame), "marker", marker, 0), 0);
}

TEST(FramePropertyShutdown, BothBufferSizeOrders) {
  for (bool reverse : {false, true}) {
    int destroyed = 0;
    {
      AviSynthEnvironment environment;
      auto* env = environment.get();
      auto parent = env->NewVideoFrame(frame_info(reverse ? 8 : 256, reverse ? 8 : 256));
      auto auxiliary = env->NewVideoFrame(frame_info(reverse ? 256 : 8, reverse ? 256 : 8));
      mark(env, auxiliary, destroyed);
      ASSERT_EQ(env->propSetFrame(env->getFramePropsRW(parent), "aux", auxiliary, 0), 0);
    }
    EXPECT_EQ(destroyed, 1);
  }
}

TEST(FramePropertyShutdown, SharedMapsAndFrameArrays) {
  int destroyed = 0;
  {
    AviSynthEnvironment environment;
    auto* env = environment.get();
    auto a = env->NewVideoFrame(frame_info());
    auto b = env->NewVideoFrame(frame_info());
    auto c = env->NewVideoFrame(frame_info());
    auto auxiliary = env->NewVideoFrame(frame_info(8, 8));
    mark(env, auxiliary, destroyed);
    ASSERT_EQ(env->propSetFrame(env->getFramePropsRW(a), "aux", auxiliary, 0), 0);
    ASSERT_EQ(env->propSetFrame(env->getFramePropsRW(a), "aux", auxiliary, 1), 0);
    env->copyFrameProps(a, b);
    env->copyFrameProps(a, c);
    // Detach c's map but retain its shared frame-array object.
    ASSERT_EQ(env->propSetInt(env->getFramePropsRW(c), "separate", 1, 0), 0);
  }
  EXPECT_EQ(destroyed, 1);
}

TEST(FramePropertyShutdown, NestedFramesAndReferenceCycles) {
  for (int shape = 0; shape < 3; ++shape) {
    int destroyed = 0;
    {
      AviSynthEnvironment environment;
      auto* env = environment.get();
      auto a = env->NewVideoFrame(frame_info(256, 256));
      auto b = env->NewVideoFrame(frame_info(64, 64));
      auto c = env->NewVideoFrame(frame_info(8, 8));
      mark(env, a, destroyed); mark(env, b, destroyed); mark(env, c, destroyed);
      auto set = [&](PVideoFrame& from, const PVideoFrame& to) {
        EXPECT_EQ(env->propSetFrame(env->getFramePropsRW(from), "ref", to, 0), 0);
      };
      if (shape == 0) { set(a, b); set(b, c); }
      if (shape == 1) { set(a, a); set(b, b); set(c, c); }
      if (shape == 2) { set(a, b); set(b, a); set(c, b); }
    }
    EXPECT_EQ(destroyed, 3);
  }
}

TEST(FramePropertyShutdown, PropertyHeldCacheCanUnregisterBeforeEnvironmentDies) {
  int destroyed = 0;
  std::mutex cache_mutex;
  {
    AviSynthEnvironment environment;
    auto* env = environment.get();
    PClip source = new DestructionMarker(destroyed);
    PClip cache = new AvsCache(source, nullptr, cache_mutex, static_cast<InternalEnvironment*>(env));
    auto parent = env->NewVideoFrame(frame_info());
    ASSERT_EQ(env->propSetClip(env->getFramePropsRW(parent), "cache", cache, 0), 0);
  }
  EXPECT_EQ(destroyed, 1);
}

struct ReentrantState {
  int callbacks = 0;
  int nested_destroyed = 0;
  int retired_sub_destroyed = 0;
  const VideoFrameBuffer* spare_buffer = nullptr;
  bool avoided_reuse = false;
  bool avoided_pruning = false;
  bool succeeded = false;
};
class ReentrantClip final : public DestructionMarker {
public:
  ReentrantClip(IScriptEnvironment* env, PVideoFrame frame, ReentrantState& state)
    : DestructionMarker(state.callbacks), env_(env), frame_(frame), state_(state) {}
  ~ReentrantClip() override {
    try {
      // Reallocate with the same geometry as zero-reference registered frames.
      // During shutdown they must not be reused or pruned under the drain.
      auto fresh = env_->NewVideoFrame(frame_info());
      state_.avoided_reuse = fresh->GetFrameBuffer() != state_.spare_buffer;
      auto sub = env_->Subframe(frame_, 0, frame_->GetPitch(), frame_->GetRowSize(), frame_->GetHeight());
      state_.avoided_pruning = state_.retired_sub_destroyed == 0;
      mark(env_, fresh, state_.nested_destroyed);
      EXPECT_EQ(env_->propSetFrame(env_->getFramePropsRW(sub), "fresh", fresh, 0), 0);
      EXPECT_EQ(env_->propSetFrame(env_->getFramePropsRW(frame_), "sub", sub, 0), 0);
      frame_ = nullptr;
      state_.succeeded = true;
    } catch (...) {
      // Plugin destructors must not throw; fail the assertion after teardown.
      state_.succeeded = false;
    }
  }
private:
  IScriptEnvironment* env_;
  PVideoFrame frame_;
  ReentrantState& state_;
};

TEST(FramePropertyShutdown, ClipDestructorCanAllocateAndRegisterFrames) {
  ReentrantState state;
  {
    AviSynthEnvironment environment;
    auto* env = environment.get();
    auto parent = env->NewVideoFrame(frame_info());
    auto spare = env->NewVideoFrame(frame_info());
    state.spare_buffer = spare->GetFrameBuffer();
    auto source = parent; // Callback also replaces the map currently being drained.
    // Leave a zero-reference subframe for RegisterSubFrame's pruning path.
    {
      auto old_sub = env->Subframe(source, 0, source->GetPitch(), source->GetRowSize(), source->GetHeight());
      mark(env, old_sub, state.retired_sub_destroyed);
    }
    PClip clip = new ReentrantClip(env, source, state);
    ASSERT_EQ(env->propSetClip(env->getFramePropsRW(parent), "callback", clip, 0), 0);
  }
  EXPECT_TRUE(state.succeeded);
  EXPECT_TRUE(state.avoided_reuse);
  EXPECT_TRUE(state.avoided_pruning);
  EXPECT_EQ(state.retired_sub_destroyed, 1);
  EXPECT_EQ(state.callbacks, 1);
  EXPECT_EQ(state.nested_destroyed, 1);
}

TEST(FramePropertyShutdown, NormalShutdownDoesNotReportLeaks) {
  testing::internal::CaptureStderr();
  {
    AviSynthEnvironment environment;
    auto* env = environment.get();
    static_cast<InternalEnvironment*>(env)->SetLogParams("stderr", LOGLEVEL_WARNING);
    auto parent = env->NewVideoFrame(frame_info());
    auto child = env->NewVideoFrame(frame_info(8, 8));
    EXPECT_EQ(env->propSetFrame(env->getFramePropsRW(parent), "child", child, 0), 0);
    EXPECT_EQ(env->propSetFrame(env->getFramePropsRW(child), "parent", parent, 0), 0);
  }
  const auto log = testing::internal::GetCapturedStderr();
  EXPECT_EQ(log.find("memory leaks"), std::string::npos) << log;
  EXPECT_EQ(log.find("ThreadScriptEnvironment leaks"), std::string::npos) << log;
}

TEST(FramePropertyShutdown, ExternalReferenceStillReportsLeak) {
  // Intentionally violate the host lifetime contract only in a child process.
  // Never release/dereference the surviving frame after its environment dies.
  EXPECT_EXIT({
    auto* env = CreateScriptEnvironment2();
    static_cast<InternalEnvironment*>(static_cast<IScriptEnvironment*>(env))->SetLogParams("stderr", LOGLEVEL_WARNING);
    auto retained = env->NewVideoFrame(frame_info());
    env->DeleteScriptEnvironment();
    std::_Exit(0);
  }, testing::ExitedWithCode(0), "A plugin or the host application might be causing memory leaks");
}

TEST(FramePropertyShutdown, SharedStorageExtractionDoesNotAllocate) {
  AVSMap original;
  auto* values = new VSArray<int64_t, AVSPropertyType::PROPERTYTYPE_INT>();
  values->push_back(42);
  original.insert("value", values);
  AVSMap shared(&original);
  bool failed = false;
  forbid_allocation = true;
  try {
    auto storage = original.storageForShutdown();
    auto node = storage->data.extract(storage->data.begin());
    // node, then storage are destroyed while allocation is still forbidden.
  } catch (...) { failed = true; }
  forbid_allocation = false;
  EXPECT_FALSE(failed);
  EXPECT_EQ(original.size(), 0u);
  EXPECT_EQ(shared.size(), 0u);
}
} // namespace
} // namespace avsut::test
