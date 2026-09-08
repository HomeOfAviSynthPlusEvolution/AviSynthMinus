#include <avisynth.h>

#include <future>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
AVSValue __cdecl BindingEven(AVSValue, void *data, IScriptEnvironment *) {
  return static_cast<int>(reinterpret_cast<uintptr_t>(data));
}
AVSValue __cdecl BindingOdd(AVSValue, void *data, IScriptEnvironment *) {
  return -1 - static_cast<int>(reinterpret_cast<uintptr_t>(data));
}
AVSValue __cdecl RegisterBatch(AVSValue args, void *, IScriptEnvironment *env) {
  for (int i = args[0].AsInt(); i < args[0].AsInt() + args[1].AsInt(); ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "CXBatchBinding%d", i);
    env->AddFunction(name, "", i % 2 ? BindingOdd : BindingEven,
        reinterpret_cast<void *>(static_cast<uintptr_t>(i + args[2].AsInt())));
  }
  return true;
}
AVSValue __cdecl BinaryStrings(AVSValue, void *, IScriptEnvironment *env) {
  // Same full DJB2 hash; differ only after NUL. strncmp incorrectly merges them.
  const char a[] = {'a', 0, 1, 33}, b[] = {'a', 0, 2, 0};
  const char *first = env->SaveString(a, sizeof(a));
  const char *second = env->SaveString(b, sizeof(b));
  return std::memcmp(first, a, sizeof(a)) == 0 &&
      std::memcmp(second, b, sizeof(b)) == 0;
}
// The host fixture supplies distinct, cached frames and interleaved stereo audio.
bool Pixels(const PVideoFrame &frame, int value) {
  for (int y = 0; y < frame->GetHeight(); ++y)
    for (int x = 0; x < frame->GetRowSize(); ++x)
      if (frame->GetReadPtr()[y * frame->GetPitch() + x] != value) return false;
  return true;
}

AVSValue __cdecl MultipleFrames(AVSValue args, void *, IScriptEnvironment *env) {
  PClip a = args[0].AsClip(), b = args[1].AsClip();
  std::vector<PVideoFrame> frames;
  frames.push_back(a->GetFrame(0, env));
  const BYTE *first = frames[0]->GetReadPtr();
  frames.push_back(a->GetFrame(1, env));
  frames.push_back(b->GetFrame(1, env));
  frames.push_back(a->GetFrame(2, env));
  return first == frames[0]->GetReadPtr() && *first == 10 &&
      Pixels(frames[0], 10) && Pixels(frames[1], 11) &&
      Pixels(frames[2], 41) && Pixels(frames[3], 12);
}

AVSValue __cdecl RetainedCow(AVSValue args, void *, IScriptEnvironment *env) {
  PClip source = args[0].AsClip();
  PVideoFrame original = source->GetFrame(0, env);
  const BYTE *pointer = original->GetReadPtr();
  PVideoFrame copy = original;
  env->MakeWritable(&copy);
  copy->GetWritePtr()[0] = 99;
  return copy->GetReadPtr()[0] == 99 && copy->GetReadPtr() != pointer &&
      original->GetReadPtr() == pointer && Pixels(original, 10) &&
      Pixels(source->GetFrame(0, env), 10);
}

AVSValue __cdecl MultipleAudio(AVSValue args, void *, IScriptEnvironment *env) {
  int a[10], b[10];
  for (int &v : a) v = -1;
  for (int &v : b) v = -1;
  args[0].AsClip()->GetAudio(a + 1, 3, 4, env);
  args[1].AsClip()->GetAudio(b + 1, 9, 4, env);
  for (int i = 0; i < 8; ++i)
    if (a[i + 1] != 1000 + 6 + i || b[i + 1] != 4000 + 18 + i) return false;
  // A zero-length read must leave the caller's buffer untouched.
  args[0].AsClip()->GetAudio(a + 1, 0, 0, env);
  return a[0] == -1 && a[9] == -1 && b[0] == -1 && b[9] == -1 && a[1] == 1006;
}

class HeldFrames final : public GenericVideoFilter {
  PClip other_;
  PVideoFrame held_;
  const BYTE *pointer_ = nullptr;
public:
  HeldFrames(PClip a, PClip b) : GenericVideoFilter(a), other_(b) {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override {
    if (!held_) {
      held_ = child->GetFrame(0, env);
      pointer_ = held_->GetReadPtr();
    }
    PVideoFrame next = other_->GetFrame(n, env);
    if (held_->GetReadPtr() != pointer_ || !Pixels(held_, 10) || !Pixels(next, 40 + n))
      env->ThrowError("retained upstream frame changed across callbacks");
    return held_;
  }
  int __stdcall SetCacheHints(int hint, int) override {
    return hint == CACHE_GET_MTMODE ? MT_SERIALIZED : 0;
  }
};
AVSValue __cdecl CreateHeld(AVSValue args, void *, IScriptEnvironment *) {
  return new HeldFrames(args[0].AsClip(), args[1].AsClip());
}

// Retain the factory's environment, then use it from a plugin-owned thread in
// a later callback. Joining keeps the environment/session lifetime explicit.
class ThreadFrame final : public GenericVideoFilter {
  IScriptEnvironment *saved_;
public:
  ThreadFrame(PClip child, IScriptEnvironment *env) : GenericVideoFilter(child), saved_(env) {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *) override {
    return std::async(std::launch::async, [this, n] {
      saved_->CheckVersion(8);
      PVideoFrame frame = child->GetFrame(n, saved_);
      saved_->MakeWritable(&frame);
      frame->GetWritePtr()[0] = 90 + n;
      return frame;
    }).get();
  }
  int __stdcall SetCacheHints(int hint, int) override {
    return hint == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
  }
};
AVSValue __cdecl CreateThread(AVSValue args, void *, IScriptEnvironment *env) {
  return new ThreadFrame(args[0].AsClip(), env);
}
AVSValue __cdecl SameFrame(AVSValue args, void *, IScriptEnvironment *env) {
  PClip clip = args[0].AsClip();
  PVideoFrame a = clip->GetFrame(0, env), b = clip->GetFrame(0, env);
  return a.operator->() == b.operator->();
}

AVSValue __cdecl ConcurrentIdentity(AVSValue args, void *, IScriptEnvironment *env) {
  PClip clip = args[0].AsClip();
  PVideoFrame anchor = clip->GetFrame(0, env);
  std::promise<void> start;
  auto ready = start.get_future().share();
  std::vector<std::future<bool>> jobs;
  for (int worker = 0; worker < 4; ++worker) {
    jobs.push_back(std::async(std::launch::async, [&, worker] {
      ready.wait();
      for (int i = 0; i < 1000; ++i) {
        const int n = (i + worker) % 3;
        PVideoFrame a = clip->GetFrame(n, env), b = clip->GetFrame(n, env);
        if (a.operator->() != b.operator->() || !Pixels(a, 10 + n)) return false;
        if (n == 0 && a.operator->() != anchor.operator->()) return false;
        // Frames 1/2 have no permanent anchor: import races with last release.
      }
      return true;
    }));
  }
  start.set_value();
  bool okay = true;
  for (auto &job : jobs) okay = job.get() && okay;
  return okay;
}

class StoredFrame final : public GenericVideoFilter {
  PVideoFrame frame_;
public:
  StoredFrame(PClip clip, PVideoFrame frame) : GenericVideoFilter(clip), frame_(frame) {}
  PVideoFrame __stdcall GetFrame(int, IScriptEnvironment *) override { return frame_; }
};
bool RoundTrip(PClip source, const PVideoFrame &frame, IScriptEnvironment *env) {
  const AVSValue clip(new StoredFrame(source, frame));
  PClip pass = env->Invoke("CXSmokePassThrough", AVSValue(&clip, 1)).AsClip();
  PVideoFrame returned = pass->GetFrame(0, env);
  return returned.operator->() == frame.operator->();
}
AVSValue __cdecl FrameRoundTrip(AVSValue args, void *, IScriptEnvironment *env) {
  PClip source = args[0].AsClip();
  PVideoFrame original = source->GetFrame(0, env);
  PVideoFrame frame = env->NewVideoFrame(source->GetVideoInfo());
  if (env->MakeWritable(&frame)) return false; // A unique new frame needs no copy.
  if (!RoundTrip(source, frame, env)) return false;
  PVideoFrame sub = env->Subframe(original, 0, original->GetPitch(),
                                  original->GetRowSize(), original->GetHeight());
  if (!RoundTrip(source, sub, env)) return false;
  frame = source->GetFrame(0, env);
  if (frame.operator->() != original.operator->()) return false;
  if (!env->MakeWritable(&frame)) return false;
  frame->GetWritePtr()[0] = 99;
  PVideoFrame again = source->GetFrame(0, env);
  return again.operator->() == original.operator->() && Pixels(original, 10) &&
      frame.operator->() != original.operator->() && RoundTrip(source, frame, env);
}
} // namespace

void RegisterBoundaryServices(IScriptEnvironment *env) {
  env->AddFunction("CXRegisterBatch", "iii", RegisterBatch, nullptr);
  env->AddFunction("CXBinaryStrings", "", BinaryStrings, nullptr);
  env->AddFunction("CXMultipleFrames", "cc", MultipleFrames, nullptr);
  env->AddFunction("CXRetainedCow", "c", RetainedCow, nullptr);
  env->AddFunction("CXMultipleAudio", "cc", MultipleAudio, nullptr);
  env->AddFunction("CXHeldFrames", "cc", CreateHeld, nullptr);
  env->AddFunction("CXThreadFrame", "c", CreateThread, nullptr);
  env->AddFunction("CXSameFrame", "c", SameFrame, nullptr);
  env->AddFunction("CXConcurrentFrameIdentity", "c", ConcurrentIdentity, nullptr);
  env->AddFunction("CXFrameRoundTripIdentity", "c", FrameRoundTrip, nullptr);
}
