// Compile this file into the plugin, not the core. Same AviSynth license and
// linking exception as avisynth.h. The legacy C++ ABI stays inside this DLL.
#include "sdk.h"
#include <atomic>
#include <array>
#include <avisynth_cx_legacy.h>
#include <avs/cx/sdk/runtime.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#ifndef _ASSERTE
#define _ASSERTE assert
#endif

namespace {
struct Session;
struct Frame;
class Environment;
// Weak proxy index: entries disappear with the last local IClip reference.
// No session-long owning cache, and no RTTI requirement on plugin projects.
std::recursive_mutex clip_mutex;
using ClipKey = std::pair<uintptr_t, uintptr_t>;
ClipKey Key(avs_cx_clip_ref_v1 r) {
  return {reinterpret_cast<uintptr_t>(r.object), reinterpret_cast<uintptr_t>(r.operations)};
}
std::map<ClipKey, IClip *> host_proxies;
std::map<IClip *, avs_cx_clip_ref_v1> host_refs;
void RetainFrame(VideoFrame *);
void ReleaseFrame(VideoFrame *);
} // namespace
class AvsCxSdkAccess {
public:
  static void Retain(IClip *p) {
    std::lock_guard<std::recursive_mutex> lock(clip_mutex);
    if (p) {
#ifdef _WIN32
      InterlockedIncrement(&p->refcnt);
#else
      __atomic_add_fetch(&p->refcnt, 1, __ATOMIC_RELAXED);
#endif
    }
  }
  static void Release(IClip *p) {
    if (!p) return;
    bool destroy;
    {
      std::lock_guard<std::recursive_mutex> lock(clip_mutex);
#ifdef _WIN32
      destroy = !InterlockedDecrement(&p->refcnt);
#else
      destroy = !__atomic_sub_fetch(&p->refcnt, 1, __ATOMIC_ACQ_REL);
#endif
      if (destroy) {
        auto it = host_refs.find(p);
        if (it != host_refs.end()) {
          host_proxies.erase(Key(it->second));
          host_refs.erase(it);
        }
      }
    }
    // Destruction may call another plugin; never hold the index lock there.
    if (destroy) delete p;
  }
  static void Retain(VideoFrame *p) {
    if (p)
      RetainFrame(p);
  }
  static void Release(VideoFrame *p) {
    if (p)
      ReleaseFrame(p);
  }
  static void Retain(IFunction *) { throw AvisynthError("CX SDK: function values unsupported"); }
  static void Release(IFunction *) {}
  static VideoFrame MakeBase() { return VideoFrame(nullptr); }
};

#include "sdk/value_methods.inc"

namespace {
using FrameKey = std::tuple<uintptr_t, uintptr_t, uintptr_t>;
FrameKey Key(avs_cx_frame_ref_v1 r, const avs_cx_sdk_feature_v1 *sdk) {
  return {reinterpret_cast<uintptr_t>(r.object), reinterpret_cast<uintptr_t>(r.operations),
          reinterpret_cast<uintptr_t>(sdk)};
}
struct FrameIndex {
  std::mutex mutex;
  std::map<FrameKey, Frame *> frames;
};
// Weak entries only. Sharding avoids serializing unrelated frame traffic.
std::array<FrameIndex, 16> frame_indices;
FrameIndex &Index(const FrameKey &key) {
  const auto address = std::get<0>(key);
  return frame_indices[((address >> 4) ^ (address >> 12)) % frame_indices.size()];
}
struct Frame : VideoFrame {
  static void *operator new(size_t n) { return ::operator new(n); }
  static void operator delete(void *p) { ::operator delete(p); }
  std::atomic<unsigned> references{0};
  const avs::cx::frame ref;
  const avs_cx_sdk_feature_v1 *sdk;
  // Plane descriptors are fetched once and are local on subsequent getters.
  avs_cx_plane_v1 planes[8]{};
  std::atomic<unsigned> mask{0};
  std::mutex plane_mutex;
  Frame(avs::cx::frame f, const avs_cx_sdk_feature_v1 *s)
      : VideoFrame(AvsCxSdkAccess::MakeBase()), ref(std::move(f)), sdk(s) {}
  int Metadata(uint32_t op, int32_t value = 0) const {
    auto r = ref.get();
    avs_cx_error_v1 e{};
    e.struct_size = sizeof(e);
    if (sdk->frame_metadata(&r, op, &value, &e) != AVS_CX_STATUS_OK)
      throw AvisynthError("CX SDK: frame metadata operation failed");
    return value;
  }
  static Frame *From(VideoFrame *p) { return static_cast<Frame *>(p); }
  static const Frame *From(const VideoFrame *p) { return static_cast<const Frame *>(p); }
  static int Index(int plane) {
    switch (plane & ~PLANAR_ALIGNED) {
    case DEFAULT_PLANE:
      return 0;
    case PLANAR_Y:
      return 1;
    case PLANAR_U:
      return 2;
    case PLANAR_V:
      return 3;
    case PLANAR_A:
      return 4;
    case PLANAR_R:
      return 5;
    case PLANAR_G:
      return 6;
    case PLANAR_B:
      return 7;
    default:
      throw AvisynthError("CX SDK: invalid plane");
    }
  }
  avs_cx_plane_v1 Plane(int plane, bool write = false) {
    const int i = Index(plane);
    avs_cx_error_v1 error{};
    error.struct_size = sizeof(error);
    if (!write && (mask.load(std::memory_order_acquire) & (1u << i)))
      return planes[i];
    std::lock_guard<std::mutex> lock(plane_mutex);
    if (write || !(mask.load(std::memory_order_relaxed) & (1u << i))) {
      avs_cx_plane_v1 p{};
      p.struct_size = sizeof(p);
      const auto status =
          ref.get_plane(plane & ~PLANAR_ALIGNED,
                        write ? AVS_CX_FRAME_ACCESS_WRITE : AVS_CX_FRAME_ACCESS_READ, &p, &error);
      if (status != AVS_CX_STATUS_OK)
        throw AvisynthError("CX SDK: frame plane unavailable");
      if (!write) {
        planes[i] = p;
        mask.fetch_or(1u << i, std::memory_order_release);
      }
      return p;
    }
    return planes[i];
  }
};
PVideoFrame Wrap(avs_cx_frame_ref_v1 ref, const avs_cx_sdk_feature_v1 *sdk) {
  auto owned = avs::cx::frame::adopt(ref);
  const auto key = Key(ref, sdk);
  auto &index = Index(key);
  // Destruction (including on allocation failure) must release host refs only
  // after unlocking. The incoming owned ref also prevents host address reuse.
  std::unique_ptr<Frame> candidate;
  std::lock_guard<std::mutex> lock(index.mutex);
  auto it = index.frames.find(key);
  if (it != index.frames.end()) return PVideoFrame(it->second);
  candidate.reset(new Frame(std::move(owned), sdk));
  index.frames.emplace(key, candidate.get());
  return PVideoFrame(candidate.release());
}
void RetainFrame(VideoFrame *p) {
  Frame::From(p)->references.fetch_add(1, std::memory_order_relaxed);
}
void ReleaseFrame(VideoFrame *p) {
  auto *f = Frame::From(p);
  const auto key = Key(f->ref.get(), f->sdk);
  auto &index = Index(key);
  bool destroy;
  {
    // Lookup+retain and the last release+erase are indivisible to each other.
    std::lock_guard<std::mutex> lock(index.mutex);
    destroy = f->references.fetch_sub(1, std::memory_order_acq_rel) == 1;
    if (destroy) index.frames.erase(key);
  }
  if (destroy) delete f;
}
Frame *WritableFrame(const VideoFrame *p) { return const_cast<Frame *>(Frame::From(p)); }
} // namespace

int VideoFrame::CX_GetPitch(int p) const { return WritableFrame(this)->Plane(p).pitch; }
int VideoFrame::CX_GetRowSize(int p) const {
  const auto plane = WritableFrame(this)->Plane(p);
  const int aligned = (plane.row_size + FRAME_ALIGN - 1) & ~(FRAME_ALIGN - 1);
  return (p & PLANAR_ALIGNED) && aligned <= plane.pitch ? aligned : plane.row_size;
}
int VideoFrame::CX_GetHeight(int p) const { return WritableFrame(this)->Plane(p).height; }
const BYTE *VideoFrame::CX_GetReadPtr(int p) const {
  return static_cast<const BYTE *>(WritableFrame(this)->Plane(p).data);
}
BYTE *VideoFrame::CX_GetWritePtr(int p) const {
  if (Frame::From(this)->references.load(std::memory_order_relaxed) != 1)
    return nullptr;
  return static_cast<BYTE *>(WritableFrame(this)->Plane(p, true).data);
}
bool VideoFrame::CX_IsWritable() const {
  if (Frame::From(this)->references.load(std::memory_order_relaxed) != 1)
    return false;
  return Frame::From(this)->Metadata(0) != 0;
}
int VideoFrame::CX_GetPixelType() const {
  uint32_t p = 0;
  const auto &f = Frame::From(this)->ref;
  if (f.get().operations->get_pixel_type(f.get().object, &p) != AVS_CX_STATUS_OK)
    throw AvisynthError("CX SDK: pixel type unavailable");
  return static_cast<int>(p);
}
void VideoFrame::CX_DESTRUCTOR() {} // Frame owns its CX reference, not this base.
VideoFrameBuffer *VideoFrame::CX_GetFrameBuffer() const {
  throw AvisynthError("CX SDK: raw frame buffers unsupported");
}
int VideoFrame::CX_GetOffset(int) const {
  throw AvisynthError("CX SDK: raw frame offsets unsupported");
}
AVSMap &VideoFrame::CX_getProperties() {
  throw AvisynthError("CX SDK: use environment frame properties");
}
const AVSMap &VideoFrame::CX_getConstProperties() {
  throw AvisynthError("CX SDK: use environment frame properties");
}
void VideoFrame::CX_setProperties(const AVSMap &) {
  throw AvisynthError("CX SDK: use environment frame properties");
}
PDevice VideoFrame::CX_GetDevice() const { throw AvisynthError("CX SDK: devices unsupported"); }
int VideoFrame::CX_CheckMemory() const { return -1; }
bool VideoFrame::CX_IsPropertyWritable() const {
  return Frame::From(this)->references.load(std::memory_order_relaxed) == 1 &&
         Frame::From(this)->Metadata(1) != 0;
}
void VideoFrame::CX_AmendPixelType(int type) {
  if (Frame::From(this)->references.load(std::memory_order_relaxed) != 1)
    throw AvisynthError("CX SDK: AmendPixelType requires a writable frame");
  Frame::From(this)->Metadata(2, type);
}

void PFunction::CX_CONSTRUCTOR0() { e = nullptr; }
void PFunction::CX_CONSTRUCTOR1(IFunction *p) {
  e = nullptr;
  if (p)
    throw AvisynthError("CX SDK: function values unsupported");
}
void PFunction::CX_CONSTRUCTOR2(const PFunction &p) { CX_CONSTRUCTOR1(p.e); }
PFunction &PFunction::CX_OPERATOR_ASSIGN0(IFunction *p) {
  CX_CONSTRUCTOR1(p);
  return *this;
}
PFunction &PFunction::CX_OPERATOR_ASSIGN1(const PFunction &p) { return CX_OPERATOR_ASSIGN0(p.e); }
void PFunction::CX_DESTRUCTOR() {}
void PDevice::CX_CONSTRUCTOR0() { e = nullptr; }
void PDevice::CX_CONSTRUCTOR1(Device *p) {
  e = nullptr;
  if (p)
    throw AvisynthError("CX SDK: devices unsupported");
}
void PDevice::CX_CONSTRUCTOR2(const PDevice &p) { CX_CONSTRUCTOR1(p.e); }
PDevice &PDevice::CX_OPERATOR_ASSIGN0(Device *p) {
  CX_CONSTRUCTOR1(p);
  return *this;
}
PDevice &PDevice::CX_OPERATOR_ASSIGN1(const PDevice &p) { return CX_OPERATOR_ASSIGN0(p.e); }
void PDevice::CX_DESTRUCTOR() {}
AvsDeviceType PDevice::CX_GetType() const { throw AvisynthError("CX SDK: devices unsupported"); }
int PDevice::CX_GetId() const { throw AvisynthError("CX SDK: devices unsupported"); }
int PDevice::CX_GetIndex() const { throw AvisynthError("CX SDK: devices unsupported"); }
const char *PDevice::CX_GetName() const { throw AvisynthError("CX SDK: devices unsupported"); }
const BYTE *VideoFrameBuffer::CX_GetReadPtr() const {
  throw AvisynthError("CX SDK: raw frame buffers unsupported");
}
BYTE *VideoFrameBuffer::CX_GetWritePtr() {
  throw AvisynthError("CX SDK: raw frame buffers unsupported");
}
int VideoFrameBuffer::CX_GetDataSize() const {
  throw AvisynthError("CX SDK: raw frame buffers unsupported");
}
int VideoFrameBuffer::CX_GetSequenceNumber() const {
  throw AvisynthError("CX SDK: raw frame buffers unsupported");
}
int VideoFrameBuffer::CX_GetRefcount() const {
  throw AvisynthError("CX SDK: raw frame buffers unsupported");
}
void VideoFrameBuffer::CX_DESTRUCTOR() {}

static INeoEnv *__stdcall CxGetNeoEnv(IScriptEnvironment *) { return nullptr; }
#include "sdk/linkage.inc"

namespace {
VideoInfo FromInfo(const avs_cx_video_info_v1 &v) {
  VideoInfo r{};
  r.width = v.width;
  r.height = v.height;
  r.fps_numerator = v.fps_numerator;
  r.fps_denominator = v.fps_denominator;
  r.num_frames = static_cast<int>(v.num_frames);
  r.pixel_type = v.pixel_type;
  r.audio_samples_per_second = v.audio_samples_per_second;
  r.sample_type = v.sample_type;
  r.nchannels = v.channels;
  r.num_audio_samples = v.num_audio_samples;
  r.image_type = v.image_type;
  return r;
}
avs_cx_video_info_v1 ToInfo(const VideoInfo &v) {
  avs_cx_video_info_v1 r{};
  r.struct_size = sizeof(r);
  r.width = v.width;
  r.height = v.height;
  r.fps_numerator = v.fps_numerator;
  r.fps_denominator = v.fps_denominator;
  r.num_frames = v.num_frames;
  r.pixel_type = v.pixel_type;
  r.audio_samples_per_second = v.audio_samples_per_second;
  r.sample_type = v.sample_type;
  r.channels = v.nchannels;
  r.num_audio_samples = v.num_audio_samples;
  r.image_type = v.image_type;
  return r;
}
void Check(avs_cx_status status, const avs_cx_error_v1 &e) {
  if (status == AVS_CX_STATUS_OK)
    return;
  if (status == AVS_CX_STATUS_FEATURE_NOT_FOUND)
    throw IScriptEnvironment::NotFound();
  // The exception is consumed before another call on this thread; Environment
  // copies host error messages to session storage when they escape to user code.
  static thread_local std::string error;
  error = e.message.data ? std::string(e.message.data, e.message.size) : "CX SDK operation failed";
  throw AvisynthError(error.c_str());
}
avs_cx_status Error(avs_cx_error_v1 *e) noexcept {
  static thread_local std::string message;
  try {
    try {
      throw;
    } catch (const AvisynthError &x) {
      message = x.msg;
    } catch (const std::exception &x) {
      message = x.what();
    } catch (...) {
      message = "CX SDK: unknown plugin exception";
    }
    avs::cx::set_error(e, AVS_CX_STATUS_PLUGIN_ERROR, message.c_str());
  } catch (...) {
    avs::cx::set_error(e, AVS_CX_STATUS_OUT_OF_MEMORY, "CX SDK: error allocation failed");
  }
  return AVS_CX_STATUS_PLUGIN_ERROR;
}
struct Binding {
  Session *session;
  IScriptEnvironment::ApplyFunc apply;
  void *data;
};
struct Session {
  const avs_cx_host_v1 *host;
  avs::cx::runtime runtime;
  const avs_cx_sdk_feature_v1 *sdk = nullptr;
  std::deque<Binding> bindings;
  std::deque<std::string> strings;
  std::vector<std::pair<IScriptEnvironment::ShutdownFunc, void *>> shutdowns;
  std::mutex mutex;
  std::unique_ptr<Environment> environment;
  ~Session();
  char *Save(const char *s, int n = -1) {
    std::lock_guard<std::mutex> lock(mutex);
    strings.emplace_back(s, n < 0 ? std::strlen(s) : static_cast<size_t>(n));
    return const_cast<char *>(strings.back().c_str());
  }
};
// Stable plugin environment, with thread-local nested call tokens.
struct CallScope {
  static thread_local CallScope *current;
  Session *session;
  const avs_cx_call_context_v1 *call;
  CallScope *previous;
  CallScope(Session *s, const avs_cx_call_context_v1 *c) : session(s), call(c), previous(current) {
    current = this;
  }
  ~CallScope() { current = previous; }
};
thread_local CallScope *CallScope::current = nullptr;
struct Values {
  std::deque<std::vector<avs_cx_value_v1>> arrays;
  std::deque<std::string> strings;
  std::vector<avs::cx::clip> clips;
  avs_cx_value_v1 To(const AVSValue &, Session *);
};
AVSValue From(const avs_cx_value_v1 &, Session *);
avs_cx_status AVS_CX_CALL Apply(void *, const avs_cx_call_context_v1 *, const avs_cx_value_v1 *,
                                uint32_t, avs_cx_value_v1 *, avs_cx_error_v1 *) noexcept;

class Environment final : public IScriptEnvironment {
public:
  Session *session;
  avs_cx_call_context_v1 call;
  Environment(Session *s, const avs_cx_call_context_v1 *c = nullptr) : session(s) {
    if (c)
      call = *c;
    else {
      call = {};
      call.struct_size = sizeof(call);
      call.abi_version = 1;
      call.host = s->host;
      call.environment = s->sdk->initialization_environment;
    }
  }
  const avs_cx_call_context_v1 &Call() const {
    for (auto *scope = CallScope::current; scope; scope = scope->previous)
      if (scope->session == session)
        return *scope->call;
    return call;
  }
  avs_cx_sdk_result_v1 Dispatch(avs_cx_sdk_request_v1 q) const {
    q.struct_size = sizeof(q);
    avs_cx_sdk_result_v1 r{};
    r.struct_size = sizeof(r);
    avs_cx_error_v1 e{};
    e.struct_size = sizeof(e);
    const auto status =
        session->sdk->dispatch(session->sdk->context, Call().environment, &q, &r, &e);
    if (status != AVS_CX_STATUS_OK) {
      if (status == AVS_CX_STATUS_FEATURE_NOT_FOUND)
        throw NotFound();
      throw AvisynthError(
          session->Save(e.message.data ? e.message.data : "CX SDK operation unsupported",
                        e.message.data ? static_cast<int>(e.message.size) : -1));
    }
    return r;
  }
  AVSValue Result(avs_cx_sdk_request_v1 q) {
    auto r = Dispatch(q);
    try {
      auto v = From(r.value, session);
      session->sdk->release_result(r.result_owner);
      return v;
    } catch (...) {
      session->sdk->release_result(r.result_owner);
      throw;
    }
  }
  int __stdcall GetCPUFlags() override {
    uint64_t f = 0;
    Check(session->runtime.get_cpu_flags(&Call(), &f), {});
    return static_cast<int>(f);
  }
  char *__stdcall SaveString(const char *s, int n = -1) override { return session->Save(s, n); }
  char *__stdcall VSprintf(const char *fmt, va_list args) override {
    va_list copy;
    va_copy(copy, args);
    int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (n < 0)
      throw AvisynthError("CX SDK: invalid format string");
    std::vector<char> b(static_cast<size_t>(n) + 1);
    std::vsnprintf(b.data(), b.size(), fmt, args);
    return SaveString(b.data(), n);
  }
  char *Sprintf(const char *fmt, ...) override {
    va_list a;
    va_start(a, fmt);
    try {
      auto *r = VSprintf(fmt, a);
      va_end(a);
      return r;
    } catch (...) {
      va_end(a);
      throw;
    }
  }
  [[noreturn]] void ThrowError(const char *fmt, ...) override {
    va_list a;
    va_start(a, fmt);
    char *s;
    try {
      s = VSprintf(fmt, a);
    } catch (...) {
      va_end(a);
      throw;
    }
    va_end(a);
    throw AvisynthError(s);
  }
  void __stdcall AddFunction(const char *n, const char *p, ApplyFunc f, void *d) override {
    Binding *binding;
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      session->bindings.push_back({session, f, d});
      binding = &session->bindings.back(); // deque references remain stable
    }
    // Never hold the SDK mutex while waiting for the host's plugin lock:
    // the thread loading a plugin can re-enter this session.
    Check(session->runtime.register_function(n, p, &Apply, binding), {});
  }
  const AVS_Linkage *__stdcall GetAVSLinkage() override { return &cx_sdk_linkage; }
  void __stdcall AtExit(ShutdownFunc f, void *d) override {
    std::lock_guard<std::mutex> lock(session->mutex);
    session->shutdowns.emplace_back(f, d);
  }
  void __stdcall CheckVersion(int v = AVISYNTH_INTERFACE_VERSION) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_CHECK_VERSION;
    q.integer = v;
    Dispatch(q);
  }
  bool __stdcall FunctionExists(const char *n) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_FUNCTION_EXISTS;
    q.text = n;
    return Dispatch(q).integer != 0;
  }
  AVSValue __stdcall Invoke(const char *n, const AVSValue a,
                            const char *const *names = nullptr) override {
    Values storage;
    auto v = storage.To(a, session);
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_INVOKE;
    q.text = n;
    q.names = names;
    q.value = &v;
    return Result(q);
  }
  AVSValue __stdcall GetVar(const char *n) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_GET_VAR;
    q.text = n;
    return Result(q);
  }
  bool Set(const char *n, const AVSValue &a, bool global) {
    Values storage;
    auto v = storage.To(a, session);
    avs_cx_sdk_request_v1 q{};
    q.operation = global ? AVS_CX_SDK_SET_GLOBAL_VAR : AVS_CX_SDK_SET_VAR;
    q.text = n;
    q.value = &v;
    return Dispatch(q).integer != 0;
  }
  bool __stdcall SetVar(const char *n, const AVSValue &a) override { return Set(n, a, false); }
  bool __stdcall SetGlobalVar(const char *n, const AVSValue &a) override { return Set(n, a, true); }
  AVSValue __stdcall GetVarDef(const char *n, const AVSValue &def = AVSValue()) override {
    try {
      return GetVar(n);
    } catch (const NotFound &) {
      return def;
    }
  }
  bool __stdcall GetVarTry(const char *n, AVSValue *v) const override {
    try {
      *v = const_cast<Environment *>(this)->GetVar(n);
      return true;
    } catch (const NotFound &) {
      return false;
    }
  }
  bool __stdcall GetVarBool(const char *n, bool def) const override {
    AVSValue v;
    return GetVarTry(n, &v) && v.IsBool() ? v.AsBool() : def;
  }
  int __stdcall GetVarInt(const char *n, int def) const override {
    AVSValue v;
    return GetVarTry(n, &v) && v.IsInt() ? v.AsInt() : def;
  }
  int64_t __stdcall GetVarLong(const char *n, int64_t def) const override {
    AVSValue v;
    return GetVarTry(n, &v) && v.IsInt() ? v.AsLong() : def;
  }
  double __stdcall GetVarDouble(const char *n, double def) const override {
    AVSValue v;
    return GetVarTry(n, &v) && v.IsFloat() ? v.AsFloat() : def;
  }
  const char *__stdcall GetVarString(const char *n, const char *def) const override {
    AVSValue v;
    return GetVarTry(n, &v) && v.IsString() ? v.AsString() : def;
  }
  bool __stdcall InvokeTry(AVSValue *r, const char *n, const AVSValue &args,
                           const char *const *names = nullptr) override {
    try {
      *r = Invoke(n, args, names);
      return true;
    } catch (const NotFound &) {
      return false;
    }
  }
  AVSValue __stdcall Invoke2(const AVSValue &last, const char *n, const AVSValue args,
                             const char *const *names = nullptr) override {
    Values storage;
    auto a = storage.To(args, session);
    auto l = storage.To(last, session);
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_INVOKE2;
    q.value = &a;
    q.other_value = &l;
    q.text = n;
    q.names = names;
    return Result(q);
  }
  bool __stdcall Invoke2Try(AVSValue *r, const AVSValue &last, const char *n, const AVSValue args,
                            const char *const *names = nullptr) override {
    try {
      *r = Invoke2(last, n, args, names);
      return true;
    } catch (const NotFound &) {
      return false;
    }
  }
  PVideoFrame __stdcall Subframe(PVideoFrame f, int o, int p, int r, int h) override {
    return Sub(f, 0, o, p, r, h, 0, 0, 0, 0);
  }
  PVideoFrame __stdcall SubframePlanar(PVideoFrame f, int o, int p, int r, int h, int u, int v,
                                       int uv) override {
    return Sub(f, 1, o, p, r, h, u, v, uv, 0);
  }
  PVideoFrame __stdcall SubframePlanarA(PVideoFrame f, int o, int p, int r, int h, int u, int v,
                                        int uv, int a) override {
    return Sub(f, 2, o, p, r, h, u, v, uv, a);
  }
  PVideoFrame Sub(PVideoFrame f, int mode, int o, int p, int r, int h, int u, int v, int uv,
                  int a) {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_SUBFRAME;
    q.integer = mode;
    q.frame = Frame::From(f.operator->())->ref.get();
    const int values[] = {o, p, r, h, u, v, uv, a};
    for (int i = 0; i < 8; ++i)
      q.integers[i] = values[i];
    auto result = Dispatch(q);
    return Wrap(result.frame, session->sdk);
  }
  bool __stdcall MakePropertyWritable(PVideoFrame *p) override {
    if (!p || !*p)
      ThrowError("CX SDK: null frame");
    if ((*p)->IsPropertyWritable())
      return false;
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_MAKE_PROPERTY_WRITABLE;
    q.frame = Frame::From(p->operator->())->ref.get();
    auto r = Dispatch(q);
    *p = Wrap(r.frame, session->sdk);
    return true;
  }
  PClip __stdcall propGetClip(const AVSMap *map, const char *key, int index, int *error) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_PROP_GET_CLIP;
    q.pointers[0] = map;
    q.pointers[1] = error;
    q.text = key;
    q.integer = index;
    const auto value = Result(q);
    return value.IsClip() ? value.AsClip() : PClip();
  }
  int __stdcall propSetClip(AVSMap *map, const char *key, PClip &clip, int append) override {
    Values storage;
    auto v = storage.To(AVSValue(clip), session);
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_PROP_SET_CLIP;
    q.pointers[0] = map;
    q.text = key;
    q.integer = append;
    q.value = &v;
    return static_cast<int>(Dispatch(q).integer);
  }
  const PVideoFrame __stdcall propGetFrame(const AVSMap *map, const char *key, int index,
                                           int *error) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_PROP_GET_FRAME;
    q.pointers[0] = map;
    q.pointers[1] = error;
    q.text = key;
    q.integer = index;
    auto r = Dispatch(q);
    if (!r.frame.object)
      return PVideoFrame();
    return Wrap(r.frame, session->sdk);
  }
  int __stdcall propSetFrame(AVSMap *map, const char *key, const PVideoFrame &f,
                             int append) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_PROP_SET_FRAME;
    q.pointers[0] = map;
    q.text = key;
    q.integer = append;
    q.frame = Frame::From(f.operator->())->ref.get();
    return static_cast<int>(Dispatch(q).integer);
  }
  void __stdcall copyFrameProps(const PVideoFrame &src, PVideoFrame &dst) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_COPY_PROPS;
    q.frame = Frame::From(src.operator->())->ref.get();
    q.other_frame = Frame::From(dst.operator->())->ref.get();
    Dispatch(q);
  }
  const AVSMap *__stdcall getFramePropsRO(const PVideoFrame &f) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_PROPS_RO;
    q.frame = Frame::From(f.operator->())->ref.get();
    return static_cast<const AVSMap *>(Dispatch(q).pointer);
  }
  AVSMap *__stdcall getFramePropsRW(PVideoFrame &f) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_PROPS_RW;
    q.frame = Frame::From(f.operator->())->ref.get();
    return static_cast<AVSMap *>(Dispatch(q).pointer);
  }
  PVideoFrame __stdcall NewVideoFrame(const VideoInfo &v, int align = FRAME_ALIGN) override {
    return NewVideoFrameP(v, nullptr, align);
  }
  PVideoFrame __stdcall NewVideoFrameP(const VideoInfo &v, const PVideoFrame *src,
                                       int align = FRAME_ALIGN) override {
    auto vi = ToInfo(v);
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_NEW_FRAME;
    q.video_info = &vi;
    q.integer = align;
    if (src && *src)
      q.frame = Frame::From(src->operator->())->ref.get();
    auto r = Dispatch(q);
    return Wrap(r.frame, session->sdk);
  }
  bool __stdcall MakeWritable(PVideoFrame *p) override {
    if (!p || !*p)
      ThrowError("CX SDK: null frame");
    auto *f = Frame::From(p->operator->());
    if ((*p)->IsWritable()) return false;
    // Never retarget an indexed wrapper: retained aliases must keep the old
    // frame and cached planes. This extra host reference also enforces local COW.
    auto writable = f->ref;
    const auto old = f->ref.get().object;
    avs_cx_error_v1 e{};
    e.struct_size = sizeof(e);
    Check(session->runtime.make_writable(&Call(), &writable, &e), e);
    const bool changed = writable.get().object != old;
    *p = Wrap(writable.detach(), session->sdk);
    return changed;
  }
  void __stdcall BitBlt(BYTE *d, int dp, const BYTE *s, int sp, int row, int h) override {
    if (row < 0 || h < 0)
      ThrowError("CX SDK: invalid BitBlt dimensions");
    for (int y = 0; y < h; ++y) {
      std::memcpy(d, s, row);
      d += dp;
      s += sp;
    }
  }
  size_t __stdcall GetEnvProperty(AvsEnvProperty p) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_ENV_PROPERTY;
    q.integer = p;
    return static_cast<size_t>(Dispatch(q).integer);
  }
  bool __stdcall AcquireGlobalLock(const char *n) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_ACQUIRE_LOCK;
    q.text = n;
    return Dispatch(q).integer != 0;
  }
  void __stdcall ReleaseGlobalLock(const char *n) override {
    avs_cx_sdk_request_v1 q{};
    q.operation = AVS_CX_SDK_RELEASE_LOCK;
    q.text = n;
    Dispatch(q);
  }
#include "sdk/environment_methods.inc"
#include "sdk/services.inc"
};

Session::~Session() = default;

class HostClip final : public IClip {
public:
  avs::cx::clip ref;
  VideoInfo vi;
  const avs_cx_sdk_feature_v1 *sdk;
  HostClip(avs::cx::clip c, const avs_cx_sdk_feature_v1 *s) : ref(std::move(c)), sdk(s) {
    avs_cx_video_info_v1 v{};
    v.struct_size = sizeof(v);
    avs_cx_error_v1 e{};
    e.struct_size = sizeof(e);
    Check(ref.get_video_info(&v, &e), e);
    vi = FromInfo(v);
  }
  const VideoInfo &__stdcall GetVideoInfo() override { return vi; }
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override {
    avs::cx::frame f;
    avs_cx_error_v1 e{};
    e.struct_size = sizeof(e);
    Check(ref.get_frame(n, &static_cast<Environment *>(env)->Call(), &f, &e), e);
    return Wrap(f.detach(), sdk);
  }
  void __stdcall GetAudio(void *p, int64_t s, int64_t n, IScriptEnvironment *env) override {
    avs_cx_error_v1 e{};
    e.struct_size = sizeof(e);
    Check(ref.get_audio(p, s, n, &static_cast<Environment *>(env)->Call(), &e), e);
  }
  bool __stdcall GetParity(int n) override {
    int32_t r = 0;
    avs_cx_error_v1 e{};
    e.struct_size = sizeof(e);
    Check(ref.get_parity(n, &r, &e), e);
    return r != 0;
  }
  int __stdcall SetCacheHints(int h, int n) override {
    int32_t r = 0;
    avs_cx_error_v1 e{};
    e.struct_size = sizeof(e);
    Check(ref.set_cache_hints(h, n, &r, &e), e);
    return r;
  }
};
struct PluginClip {
  std::atomic<unsigned> references{1};
  PClip clip;
  Session *session;
  PluginClip(PClip p, Session *s) : clip(p), session(s) {}
  static const avs_cx_clip_ops_v1 &Ops() {
    static const auto ops = [] {
      avs_cx_clip_ops_v1 o{};
      o.struct_size = sizeof(o);
      o.abi_version = 1;
      o.retain = [](void *p) {
        static_cast<PluginClip *>(p)->references.fetch_add(1, std::memory_order_relaxed);
      };
      o.release = [](void *p) {
        auto *s = static_cast<PluginClip *>(p);
        if (s->references.fetch_sub(1, std::memory_order_acq_rel) == 1)
          delete s;
      };
      o.get_video_info = [](void *p, avs_cx_video_info_v1 *v, avs_cx_error_v1 *e) -> avs_cx_status {
        try {
          *v = ToInfo(static_cast<PluginClip *>(p)->clip->GetVideoInfo());
          return 0;
        } catch (...) {
          return Error(e);
        }
      };
      o.get_frame = [](void *p, int64_t n, const avs_cx_call_context_v1 *c, avs_cx_frame_ref_v1 *r,
                       avs_cx_error_v1 *e) -> avs_cx_status {
        try {
          auto *s = static_cast<PluginClip *>(p);
          CallScope scope(s->session, c);
          auto &env = *s->session->environment;
          PVideoFrame f = s->clip->GetFrame(static_cast<int>(n), &env);
          if (!f)
            throw AvisynthError("CX SDK: plugin returned null frame");
          auto ref = Frame::From(f.operator->())->ref;
          *r = ref.detach();
          return 0;
        } catch (...) {
          return Error(e);
        }
      };
      o.get_audio = [](void *p, void *b, int64_t n, int64_t count, const avs_cx_call_context_v1 *c,
                       avs_cx_error_v1 *e) -> avs_cx_status {
        try {
          auto *s = static_cast<PluginClip *>(p);
          CallScope scope(s->session, c);
          auto &env = *s->session->environment;
          s->clip->GetAudio(b, n, count, &env);
          return 0;
        } catch (...) {
          return Error(e);
        }
      };
      o.get_parity = [](void *p, int64_t n, int32_t *r, avs_cx_error_v1 *e) -> avs_cx_status {
        try {
          *r = static_cast<PluginClip *>(p)->clip->GetParity(static_cast<int>(n));
          return 0;
        } catch (...) {
          return Error(e);
        }
      };
      o.set_cache_hints = [](void *p, int32_t h, int32_t n, int32_t *r,
                             avs_cx_error_v1 *e) -> avs_cx_status {
        try {
          *r = static_cast<PluginClip *>(p)->clip->SetCacheHints(h, n);
          return 0;
        } catch (...) {
          return Error(e);
        }
      };
      return o;
    }();
    return ops;
  }
};
AVSValue WrapClip(avs_cx_clip_ref_v1 ref, Session *s) {
  const auto key = Key(ref);
  {
    std::lock_guard<std::recursive_mutex> lock(clip_mutex);
    auto it = host_proxies.find(key);
    if (it != host_proxies.end()) return AVSValue(it->second);
  }
  // Fetch video information outside the index lock: it can invoke another DLL.
  std::unique_ptr<HostClip> candidate(new HostClip(avs::cx::clip::retain(ref), s->sdk));
  std::lock_guard<std::recursive_mutex> lock(clip_mutex);
  auto it = host_proxies.find(key);
  if (it != host_proxies.end()) return AVSValue(it->second);
  auto *p = candidate.get();
  host_refs.emplace(p, ref);
  try { host_proxies.emplace(key, p); }
  catch (...) { host_refs.erase(p); throw; }
  candidate.release();
  return AVSValue(p);
}

AVSValue From(const avs_cx_value_v1 &v, Session *s) {
  switch (v.type) {
  case AVS_CX_VALUE_UNDEFINED:
    return {};
  case AVS_CX_VALUE_BOOL:
    return v.value.boolean != 0;
  case AVS_CX_VALUE_INT:
    return AVSValue(v.value.integer);
  case AVS_CX_VALUE_INT32:
    return AVSValue(static_cast<int>(v.value.integer));
  case AVS_CX_VALUE_FLOAT:
    return v.value.floating_point;
  case AVS_CX_VALUE_FLOAT32:
    return AVSValue(static_cast<float>(v.value.floating_point));
  case AVS_CX_VALUE_STRING:
    return s->Save(v.value.string.data, static_cast<int>(v.value.string.size));
  case AVS_CX_VALUE_CLIP:
    return WrapClip(v.value.clip, s);
  case AVS_CX_VALUE_ARRAY: {
    if (v.value.array.size > static_cast<uint32_t>((std::numeric_limits<short>::max)()))
      throw AvisynthError("CX SDK: array exceeds legacy AVSValue capacity");
    std::vector<AVSValue> a;
    a.reserve(v.value.array.size);
    for (uint32_t i = 0; i < v.value.array.size; ++i)
      a.push_back(From(v.value.array.values[i], s));
    return AVSValue(a.data(), static_cast<int>(a.size()));
  }
  default:
    throw AvisynthError("CX SDK: unknown value type");
  }
}
avs_cx_value_v1 Values::To(const AVSValue &v, Session *s) {
  avs_cx_value_v1 r{};
  r.struct_size = sizeof(r);
  if (!v.Defined())
    return r;
  if (v.IsBool()) {
    r.type = AVS_CX_VALUE_BOOL;
    r.value.boolean = v.AsBool();
  } else if (v.IsInt()) {
    r.type = v.IsLongStrict() ? AVS_CX_VALUE_INT : AVS_CX_VALUE_INT32;
    r.value.integer = v.AsLong();
  } else if (v.IsFloat()) {
    r.type = v.IsFloatfStrict() ? AVS_CX_VALUE_FLOAT32 : AVS_CX_VALUE_FLOAT;
    r.value.floating_point = v.AsFloat();
  } else if (v.IsString()) {
    r.type = AVS_CX_VALUE_STRING;
    strings.emplace_back(v.AsString());
    r.value.string = avs::cx::string_view(strings.back().c_str());
  } else if (v.IsClip()) {
    r.type = AVS_CX_VALUE_CLIP;
    PClip p = v.AsClip();
    if (!p) {
      r.type = AVS_CX_VALUE_UNDEFINED;
      return r;
    }
    avs_cx_clip_ref_v1 host{};
    {
      std::lock_guard<std::recursive_mutex> lock(clip_mutex);
      auto it = host_refs.find(p.operator->());
      if (it != host_refs.end()) host = it->second;
    }
    if (host.object)
      clips.push_back(avs::cx::clip::retain(host));
    else {
      auto *c = new PluginClip(p, s);
      clips.push_back(avs::cx::clip::adopt({c, &PluginClip::Ops()}));
    }
    r.value.clip = clips.back().get();
  } else if (v.IsArray()) {
    r.type = AVS_CX_VALUE_ARRAY;
    arrays.emplace_back(v.ArraySize());
    auto &a = arrays.back();
    for (int i = 0; i < v.ArraySize(); ++i)
      a[i] = To(v[i], s);
    r.value.array.values = a.data();
    r.value.array.size = static_cast<uint32_t>(a.size());
  } else
    throw AvisynthError("CX SDK: function values unsupported");
  return r;
}
avs_cx_status AVS_CX_CALL Apply(void *p, const avs_cx_call_context_v1 *c, const avs_cx_value_v1 *a,
                                uint32_t n, avs_cx_value_v1 *r, avs_cx_error_v1 *e) noexcept {
  try {
    auto *b = static_cast<Binding *>(p);
    CallScope scope(b->session, c);
    auto &env = *b->session->environment;
    std::vector<AVSValue> args;
    args.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
      args.push_back(From(a[i], b->session));
    AVSValue result = b->apply(AVSValue(args.data(), static_cast<int>(n)), b->data, &env);
    // Only returned string/array storage is borrowed until the next callback.
    Values prepared;
    const auto converted = prepared.To(result, b->session);
    static thread_local Values output;
    output = std::move(prepared);
    *r = converted;
    for (auto &clip : output.clips)
      clip.detach();
    return 0;
  } catch (...) {
    return Error(e);
  }
}
void AVS_CX_CALL Shutdown(void *p) noexcept {
  std::unique_ptr<Session> s(static_cast<Session *>(p));
  auto &env = *s->environment;
  for (auto it = s->shutdowns.rbegin(); it != s->shutdowns.rend(); ++it) {
    try {
      it->first(it->second, &env);
    } catch (...) {
    }
  }
}
} // namespace
avs_cx_status avs::cx::initialize_legacy(const avs_cx_host_v1 *host, legacy_init init) noexcept {
  try {
    std::unique_ptr<Session> s(new Session);
    s->host = host;
    auto status = s->runtime.initialize(host);
    if (status != 0)
      return status;
    const void *feature = nullptr;
    status = host->query_feature(host->host_context, AVS_CX_FEATURE_SDK, 1,
                                 sizeof(avs_cx_sdk_feature_v1), &feature);
    if (status != 0)
      return status;
    s->sdk = static_cast<const avs_cx_sdk_feature_v1 *>(feature);
    s->environment.reset(new Environment(s.get()));
    status = s->runtime.register_shutdown(&Shutdown, s.get());
    if (status != 0)
      return status;
    auto *session = s.release(); // Host owns rollback and successful shutdown.
    auto &env = *session->environment;
    const char *name = init(&env, &cx_sdk_linkage);
    return session->runtime.set_plugin_name(name ? name : "CX legacy SDK plugin");
  } catch (...) {
    return AVS_CX_STATUS_PLUGIN_ERROR;
  }
}
