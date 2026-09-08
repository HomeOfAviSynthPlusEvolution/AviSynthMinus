#include <avisynth.h>
#include <avs/cx/abi.h>

#include "checker_kernel.h"

namespace {

class LegacyCheckerInvert final : public GenericVideoFilter {
public:
  LegacyCheckerInvert(PClip child, int block_size, IScriptEnvironment *environment)
      : GenericVideoFilter(child), block_size_(block_size) {
    if (!vi.IsY8()) {
      environment->ThrowError("CXCheckerInvert: the smoke filter currently requires Y8");
    }
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *environment) override {
    PVideoFrame frame = child->GetFrame(n, environment);
    environment->MakeWritable(&frame);
    cx_smoke::InvertCheckerboard(frame->GetWritePtr(PLANAR_Y), frame->GetPitch(PLANAR_Y),
                                 frame->GetRowSize(PLANAR_Y), frame->GetHeight(PLANAR_Y),
                                 block_size_);
    return frame;
  }

  int __stdcall SetCacheHints(int cache_hints, int) override {
    return cache_hints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
  }

private:
  int block_size_;
};

class LegacyPassThrough final : public GenericVideoFilter {
public:
  explicit LegacyPassThrough(PClip child) : GenericVideoFilter(child) {}

  int __stdcall SetCacheHints(int cache_hints, int) override {
    return cache_hints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
  }
};

AVSValue __cdecl CreateChecker(AVSValue arguments, void *, IScriptEnvironment *environment) {
  const int block_size = arguments[1].AsInt(16);
  if (block_size <= 0) {
    environment->ThrowError("CXCheckerInvert: block must be greater than zero");
  }
  return new LegacyCheckerInvert(arguments[0].AsClip(), block_size, environment);
}

AVSValue __cdecl CreatePassThrough(AVSValue arguments, void *, IScriptEnvironment *) {
  return new LegacyPassThrough(arguments[0].AsClip());
}

// The same source exercises Init3 and the source-compatible CX adapter.
AVSValue __cdecl AcquireLock(AVSValue arguments, void *, IScriptEnvironment *environment) {
  return environment->AcquireGlobalLock(arguments[0].AsString());
}

AVSValue __cdecl ReleaseLock(AVSValue arguments, void *, IScriptEnvironment *environment) {
  environment->ReleaseGlobalLock(arguments[0].AsString());
  return true;
}

AVSValue __cdecl GlobalLockRoundTrip(AVSValue, void *, IScriptEnvironment *environment) {
  constexpr char name[] = "avs-cx-smoke-global-lock";
  if (!environment->AcquireGlobalLock(name))
    environment->ThrowError("global-lock acquire failed");
  environment->ReleaseGlobalLock(name);
  return true;
}

} // namespace

const AVS_Linkage *AVS_linkage = nullptr;
void RegisterLegacyServices(IScriptEnvironment *);

extern "C" AVS_CX_EXPORT const char *__stdcall
AvisynthPluginInit3(IScriptEnvironment *environment, const AVS_Linkage *const linkage) {
  AVS_linkage = linkage;
  RegisterLegacyServices(environment);
  environment->AddFunction("CXCheckerInvert", "c[block]i", &CreateChecker, nullptr);
#if defined(_MSC_VER)
  environment->AddFunction("CXMsvcCheckerInvert", "c[block]i", &CreateChecker, nullptr);
#else
  environment->AddFunction("CXGccCheckerInvert", "c[block]i", &CreateChecker, nullptr);
#endif
  environment->AddFunction("CXSmokePassThrough", "c", &CreatePassThrough, nullptr);
  environment->AddFunction("CXGlobalLockRoundTrip", "", &GlobalLockRoundTrip, nullptr);
  environment->AddFunction("CXAcquireLock", "s", &AcquireLock, nullptr);
  environment->AddFunction("CXReleaseLock", "s", &ReleaseLock, nullptr);
  return "AviSynth CX smoke plugin (legacy Init3)";
}
