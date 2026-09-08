// Conventional plugin code: no CX types, helpers, or conditional paths.
#include <avisynth.h>
#include <cstring>
#include <stdexcept>

namespace {
class Services final : public GenericVideoFilter {
  IScriptEnvironment *saved_env;

public:
  Services(PClip c, IScriptEnvironment *env) : GenericVideoFilter(c), saved_env(env) {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override {
    if (n == 7)
      throw std::runtime_error("legacy GetFrame exception");
    PVideoFrame source = child->GetFrame(n, env);
    if (saved_env->GetCPUFlags() != env->GetCPUFlags())
      env->ThrowError("saved environment failed");
    PVideoFrame output = env->NewVideoFrameP(vi, &source);
    env->BitBlt(output->GetWritePtr(), output->GetPitch(), source->GetReadPtr(), source->GetPitch(),
                source->GetRowSize(), source->GetHeight());
    auto *props = env->getFramePropsRW(output);
    env->propSetInt(props, "SdkFrame", n, 0);
    int error = 0;
    if (env->propGetInt(env->getFramePropsRO(output), "SdkFrame", 0, &error) != n || error)
      env->ThrowError("frame property roundtrip failed");
    return output;
  }
  int __stdcall SetCacheHints(int h, int) override {
    return h == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
  }
};
AVSValue __cdecl CreateServices(AVSValue a, void *, IScriptEnvironment *env) {
  env->CheckVersion(8);
  env->SetVar("SdkNumber", AVSValue(int64_t(1) << 40));
  if (env->GetVarLong("SdkNumber", 0) != (int64_t(1) << 40))
    env->ThrowError("int64 variable failed");
  if (env->GetVarInt("SdkMissing", 91) != 91)
    env->ThrowError("default variable failed");
  const AVSValue arg(25);
  if (env->Invoke("Sqrt", AVSValue(&arg, 1)).AsFloat() != 5.0)
    env->ThrowError("nested Invoke failed");
  try {
    env->Invoke("SdkNoSuchFunction", AVSValue(nullptr, 0));
    env->ThrowError("NotFound missing");
  } catch (const IScriptEnvironment::NotFound &) {
  }
  return new Services(a[0].AsClip(), env);
}
AVSValue __cdecl Echo(AVSValue a, void *, IScriptEnvironment *) { return a[0]; }
AVSValue __cdecl ValueType(AVSValue a, void *, IScriptEnvironment *) {
  return int(a[0].GetType());
}
AVSValue __cdecl SameClip(AVSValue a, void *, IScriptEnvironment *) {
  return a[0].AsClip().operator->() == a[1].AsClip().operator->();
}
AVSValue __cdecl WriteProbe(AVSValue a, void *, IScriptEnvironment *env) {
  PVideoFrame frame = a[0].AsClip()->GetFrame(0, env);
  return frame->GetWritePtr() == nullptr;
}
AVSValue __cdecl Answer(AVSValue, void *, IScriptEnvironment *) { return 42; }
AVSValue __cdecl LateRegistration(AVSValue, void *, IScriptEnvironment *env) {
  env->AddFunction("CXRegisteredLate", "", Answer, nullptr);
  return env->FunctionExists("CXRegisteredLate") &&
         env->Invoke("CXRegisteredLate", AVSValue(nullptr, 0)).AsInt() == 42;
}
AVSValue __cdecl CopyOnWrite(AVSValue a, void *, IScriptEnvironment *env) {
  const VideoInfo vi = a[0].AsClip()->GetVideoInfo();
  PVideoFrame first = env->NewVideoFrame(vi);
  first->GetWritePtr()[0] = 37;
  PVideoFrame second = first;
  env->MakeWritable(&second);
  second->GetWritePtr()[0] = 99;
  if (first->GetReadPtr()[0] != 37 || second->GetReadPtr()[0] != 99)
    return false;
  env->propSetInt(env->getFramePropsRW(first), "CowProp", 11, 0);
  PVideoFrame props = first;
  env->MakePropertyWritable(&props);
  env->propSetInt(env->getFramePropsRW(props), "CowProp", 22, 0);
  int error = 0;
  if (env->propGetInt(env->getFramePropsRO(first), "CowProp", 0, &error) != 11 || error)
    return false;
  if (first->GetReadPtr() != props->GetReadPtr())
    return false;
  PVideoFrame sub = env->Subframe(first, first->GetPitch(), first->GetPitch(), first->GetRowSize(),
                                  first->GetHeight() - 1);
  return sub->GetReadPtr() == first->GetReadPtr() + first->GetPitch();
}
} // namespace
void RegisterLegacyServices(IScriptEnvironment *env) {
  env->AddFunction("CXRegisteredDuringInit", "", Answer, nullptr);
  if (!env->FunctionExists("CXRegisteredDuringInit") ||
      env->Invoke("CXRegisteredDuringInit", AVSValue(nullptr, 0)).AsInt() != 42)
    env->ThrowError("registration must be immediately visible");
  env->AddFunction("CXRegisterLate", "", LateRegistration, nullptr);
  env->AddFunction("CXValueType", ".", ValueType, nullptr);
  env->AddFunction("CXSameClip", "cc", SameClip, nullptr);
  env->AddFunction("CXWriteProbe", "c", WriteProbe, nullptr);
  env->AddFunction("CXServices", "c", CreateServices, nullptr);
  env->AddFunction("CXEcho", ".", Echo, nullptr);
  env->AddFunction("CXCow", "c", CopyOnWrite, nullptr);
}
