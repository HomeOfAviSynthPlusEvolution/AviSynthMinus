#include <avisynth.h>
#include <cstdint>
const AVS_Linkage *AVS_linkage = nullptr;
namespace {
AVSValue __cdecl Flags(AVSValue, void *, IScriptEnvironment *env) {
#if V12_FULL
  return env->GetCPUFlagsEx();
#else
  return int64_t(uint32_t(env->GetCPUFlags()));
#endif
}
AVSValue __cdecl Lock(AVSValue, void *, IScriptEnvironment *env) {
#if V12_LOCK
  env->CheckVersion(12);
  if (!env->AcquireGlobalLock("v12-cross-sdk")) return false;
  env->ReleaseGlobalLock("v12-cross-sdk");
#endif
  return true;
}
AVSValue __cdecl L2(AVSValue, void *, IScriptEnvironment *env) {
#if V12_FULL
  return int64_t(env->GetEnvProperty(AEP_CACHESIZE_L2));
#else
  return int64_t(0);
#endif
}
class Message final : public GenericVideoFilter {
  bool ex, utf8;
  const char *text;
public:
  Message(PClip c, const char *s, bool e, bool u) : GenericVideoFilter(c), ex(e), utf8(u), text(s) {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override {
    PVideoFrame frame = child->GetFrame(n, env);
    env->MakeWritable(&frame);
#if V12_FULL
    if (ex) env->ApplyMessageEx(&frame, vi, text, 128, 0xFFFFFF, 0, 0, utf8);
    else
#endif
      env->ApplyMessage(&frame, vi, text, 128, 0xFFFFFF, 0, 0);
    return frame;
  }
  int __stdcall SetCacheHints(int h, int) override { return h == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0; }
};
AVSValue __cdecl Draw(AVSValue a, void *, IScriptEnvironment *env) {
  return new Message(a[0].AsClip(), env->SaveString(a[1].AsString()), a[2].AsBool(), a[3].AsBool());
}
class ThreadProbe final : public GenericVideoFilter {
  int mode, threads = 0, calls = 0;
public:
  ThreadProbe(PClip c, int m) : GenericVideoFilter(c), mode(m) {}
  int __stdcall SetCacheHints(int h, int n) override {
    if (h == CACHE_GET_MTMODE) return mode;
#if V12_FULL
    if (h == CACHE_INFORM_NUM_THREADS) { threads = n; ++calls; }
#endif
    return 0;
  }
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override {
    PVideoFrame f = child->GetFrame(n, env);
    env->MakePropertyWritable(&f);
    AVSMap *p = env->getFramePropsRW(f);
    env->propSetInt(p, "V12Threads", threads, 0);
    env->propSetInt(p, "V12Notifications", calls, 0);
    return f;
  }
};
AVSValue __cdecl Probe(AVSValue a, void *, IScriptEnvironment *) {
  return new ThreadProbe(a[0].AsClip(), a[1].AsInt());
}
}
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
extern "C" EXPORT const char *__stdcall AvisynthPluginInit3(IScriptEnvironment *env, const AVS_Linkage *linkage) {
  AVS_linkage = linkage;
  env->CheckVersion(V12_LOCK ? 12 : 11);
  env->AddFunction("V12Flags", "", Flags, nullptr);
  env->AddFunction("V12Lock", "", Lock, nullptr);
  env->AddFunction("V12L2", "", L2, nullptr);
  env->AddFunction("V12Draw", "csbb", Draw, nullptr);
  env->AddFunction("V12Probe", "ci", Probe, nullptr);
  return "cross-SDK compatibility fixture";
}
