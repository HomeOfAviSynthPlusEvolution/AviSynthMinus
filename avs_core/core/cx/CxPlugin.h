#ifndef AVSCORE_CX_PLUGIN_H
#define AVSCORE_CX_PLUGIN_H

#include <avisynth.h>
#include <avs/cx/environment.h>
#include <avs/cx/feature_keys.h>
#include <avs/cx/registry.h>
#include <avs/cx/sdk.h>

#include <string>
#include <list>
#include <vector>

class InternalEnvironment;
class PluginManager;

class CxCoreFrameAccess {
public:
  static void AddRef(VideoFrame *frame);
  static void AddRef(IClip *clip);
  static void Release(IClip *clip);
  static void Release(VideoFrame *frame);
  static void Adopt(PVideoFrame *destination, VideoFrame *frame) noexcept;
  static VideoFrame *Detach(PVideoFrame *source) noexcept;
};

class CxHostSession {
public:
  CxHostSession(PluginManager *manager, InternalEnvironment *environment);
  ~CxHostSession();

  const avs_cx_host_v1 *Host() const noexcept { return &host_; }
  const std::string &PluginName() const noexcept { return plugin_name_; }
  const std::string &LastError() const noexcept { return last_error_; }
  avs_cx_status Commit() noexcept;

private:
  static avs_cx_status AVS_CX_CALL SdkDispatch(void *, void *, const avs_cx_sdk_request_v1 *,
                                               avs_cx_sdk_result_v1 *, avs_cx_error_v1 *) noexcept;
  static avs_cx_status AVS_CX_CALL QueryFeature(void *context, uint64_t feature_key,
                                                uint32_t exact_version,
                                                uint32_t minimum_struct_size,
                                                const void **feature_out) noexcept;
  static avs_cx_status AVS_CX_CALL SetPluginName(void *context,
                                                 const avs_cx_string_view_v1 *name) noexcept;
  static avs_cx_status AVS_CX_CALL RegisterFunction(void *context,
                                                    const avs_cx_string_view_v1 *name,
                                                    const avs_cx_string_view_v1 *parameter_string,
                                                    avs_cx_apply_function_v1 apply,
                                                    void *plugin_user_data) noexcept;
  static avs_cx_status AVS_CX_CALL RegisterShutdown(void *context, avs_cx_shutdown_v1 shutdown,
                                                    void *plugin_user_data) noexcept;
  static avs_cx_status AVS_CX_CALL MakeWritable(void *context, void *environment,
                                                avs_cx_frame_ref_v1 *frame,
                                                avs_cx_error_v1 *error_out) noexcept;
  static avs_cx_status AVS_CX_CALL GetCpuFlags(void *context, void *environment,
                                               uint64_t *flags_out) noexcept;

  static AVSValue __cdecl ApplyBridge(AVSValue arguments, void *user_data,
                                      IScriptEnvironment *environment);
  static void __cdecl ShutdownBridge(void *user_data, IScriptEnvironment *environment);

  void SetLastError(const char *message) noexcept;
  void RemoveFunctions(void *binding = nullptr) noexcept;

  struct PendingShutdown {
    avs_cx_shutdown_v1 shutdown;
    void *plugin_user_data;
  };

  struct CxFunctionBinding {
    CxHostSession *session;
    avs_cx_apply_function_v1 apply;
    void *plugin_user_data;
  };
  // Typed storage with stable addresses, guarded by the host registration lock.
  // Remove function-table entries before destroying their bindings.
  std::list<CxFunctionBinding> bindings_;

  PluginManager *manager_;
  InternalEnvironment *environment_;
  avs_cx_host_v1 host_{};
  avs_cx_registry_feature_v1 registry_{};
  avs_cx_environment_feature_v1 environment_feature_{};
  avs_cx_clip_feature_v1 clip_feature_{};
  avs_cx_frame_feature_v1 frame_feature_{};
  avs_cx_sdk_feature_v1 sdk_feature_{};
  std::string plugin_name_;
  std::string last_error_;
  std::vector<PendingShutdown> pending_shutdowns_;
  bool committed_ = false;
};

#endif // AVSCORE_CX_PLUGIN_H
