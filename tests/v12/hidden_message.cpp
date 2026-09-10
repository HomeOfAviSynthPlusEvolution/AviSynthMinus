#include <avisynth_cx_legacy.h>
#include <avs/cx/registry.h>
#include <avs/cx/feature_keys.h>
#include <memory>
#include <cstring>
extern "C" const char *__stdcall AvisynthPluginInit3(IScriptEnvironment *, const AVS_Linkage *);
namespace {
struct HostProxy { avs_cx_host_v1 host; const avs_cx_host_v1 *original; };
avs_cx_status AVS_CX_CALL Query(void *p, uint64_t key, uint32_t version, uint32_t size, const void **out) {
  auto *proxy = static_cast<HostProxy *>(p);
  if (key == AVS_CX_FEATURE_MESSAGE) { if (out) *out = nullptr; return AVS_CX_STATUS_FEATURE_NOT_FOUND; }
  return proxy->original->query_feature(proxy->original->host_context, key, version, size, out);
}
AVSValue __cdecl Verified(AVSValue, void *, IScriptEnvironment *) { return true; }
const char *__stdcall InitializeProbe(IScriptEnvironment *env, const AVS_Linkage *linkage) {
  AvisynthPluginInit3(env, linkage);
  // Exercise the proxy while its initialization call context is active.
  env->GetCPUFlagsEx();
  if (!env->AcquireGlobalLock("v12-feature-probe")) env->ThrowError("lock failed");
  env->ReleaseGlobalLock("v12-feature-probe");
  VideoInfo vi{}; vi.width = 64; vi.height = 32; vi.pixel_type = VideoInfo::CS_BGR32;
  auto f = env->NewVideoFrame(vi);
  bool unsupported = false;
  try { env->ApplyMessageEx(&f, vi, "test", 128, 0xFFFFFF, 0, 0, true); }
  catch (const AvisynthError &e) { unsupported = std::strstr(e.msg, "does not support message rendering") != nullptr; }
  if (!unsupported) env->ThrowError("missing feature was not reported");
  env->AddFunction("V12MissingVerified", "", Verified, nullptr);
  return "missing feature negotiation fixture";
}
void AVS_CX_CALL Destroy(void *p) { delete static_cast<HostProxy *>(p); }
}
extern "C" AVS_CX_EXPORT avs_cx_status AVS_CX_CALL AvisynthPluginInitCX1(const avs_cx_host_v1 *host) noexcept {
  try {
    std::unique_ptr<HostProxy> p(new HostProxy{*host, host});
    p->host.host_context = p.get(); p->host.query_feature = Query;
    const void *raw = nullptr;
    auto s = host->query_feature(host->host_context, AVS_CX_FEATURE_REGISTRY, 1,
                               sizeof(avs_cx_registry_feature_v1), &raw);
    if (s) return s;
    auto *r = static_cast<const avs_cx_registry_feature_v1 *>(raw);
    s = r->register_shutdown(r->context, Destroy, p.get());
    if (s) return s;
    // SDK shutdown is registered later and runs before this proxy's destruction.
    auto *owned = p.release();
    return avs::cx::initialize_legacy(&owned->host, InitializeProbe);
  } catch (...) { return AVS_CX_STATUS_PLUGIN_ERROR; }
}
