// Internal RAII support for the source-compatible plugin SDK.
//
// This header deliberately includes the legacy declarations so one plugin
// binary can also provide AvisynthPluginInit3. The avs::cx layer itself only
// communicates through the C ABI in avs/cx/*.h.
#ifndef AVS_CX_SDK_RUNTIME_H
#define AVS_CX_SDK_RUNTIME_H

#include <avisynth.h>
#include <avs/cx/clip.h>
#include <avs/cx/environment.h>
#include <avs/cx/feature_keys.h>
#include <avs/cx/registry.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace avs {
namespace cx {

inline avs_cx_string_view_v1 string_view(const char *text) noexcept {
  avs_cx_string_view_v1 result{};
  result.data = text;
  result.size = text == nullptr ? 0U : static_cast<uint32_t>(std::strlen(text));
  return result;
}

inline void set_error(avs_cx_error_v1 *error, avs_cx_status code, const char *message) noexcept {
  if (error == nullptr || error->struct_size < sizeof(*error)) {
    return;
  }
  *error = {};
  error->struct_size = sizeof(*error);
  error->code = code;
  error->message = string_view(message);
}

class frame {
public:
  frame() noexcept = default;

  static frame adopt(avs_cx_frame_ref_v1 value) noexcept { return frame(value, false); }

  static frame retain(avs_cx_frame_ref_v1 value) noexcept { return frame(value, true); }

  frame(const frame &other) noexcept : value_(other.value_) { retain_value(); }

  frame(frame &&other) noexcept : value_(other.detach()) {}

  frame &operator=(const frame &other) noexcept {
    if (this != &other) {
      reset();
      value_ = other.value_;
      retain_value();
    }
    return *this;
  }

  frame &operator=(frame &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = other.detach();
    }
    return *this;
  }

  ~frame() { reset(); }

  explicit operator bool() const noexcept {
    return value_.object != nullptr && value_.operations != nullptr &&
           value_.operations->struct_size >= sizeof(avs_cx_frame_ops_v1) &&
           value_.operations->abi_version == AVS_CX_ABI_VERSION_1 &&
           value_.operations->retain != nullptr && value_.operations->release != nullptr;
  }

  avs_cx_status get_plane(uint32_t plane, uint32_t access, avs_cx_plane_v1 *result,
                          avs_cx_error_v1 *error = nullptr) const noexcept {
    if (!*this || result == nullptr || value_.operations->get_plane == nullptr) {
      set_error(error, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid CX frame reference");
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    }
    *result = {};
    result->struct_size = sizeof(*result);
    return value_.operations->get_plane(value_.object, plane, access, result, error);
  }

  avs_cx_status get_pixel_type(uint32_t *result) const noexcept {
    if (!*this || result == nullptr || value_.operations->get_pixel_type == nullptr) {
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    }
    return value_.operations->get_pixel_type(value_.object, result);
  }

  avs_cx_frame_ref_v1 get() const noexcept { return value_; }

  avs_cx_frame_ref_v1 detach() noexcept {
    const avs_cx_frame_ref_v1 result = value_;
    value_ = {};
    return result;
  }

  void reset() noexcept {
    if (value_.object != nullptr && value_.operations != nullptr &&
        value_.operations->release != nullptr) {
      value_.operations->release(value_.object);
    }
    value_ = {};
  }

private:
  explicit frame(avs_cx_frame_ref_v1 value, bool add_reference) noexcept : value_(value) {
    if (add_reference) {
      retain_value();
    }
  }

  void retain_value() noexcept {
    if (value_.object != nullptr && value_.operations != nullptr &&
        value_.operations->retain != nullptr) {
      value_.operations->retain(value_.object);
    }
  }

  avs_cx_frame_ref_v1 value_{};
};

class clip {
public:
  clip() noexcept = default;

  static clip adopt(avs_cx_clip_ref_v1 value) noexcept { return clip(value, false); }

  static clip retain(avs_cx_clip_ref_v1 value) noexcept { return clip(value, true); }

  clip(const clip &other) noexcept : value_(other.value_) { retain_value(); }
  clip(clip &&other) noexcept : value_(other.detach()) {}

  clip &operator=(const clip &other) noexcept {
    if (this != &other) {
      reset();
      value_ = other.value_;
      retain_value();
    }
    return *this;
  }

  clip &operator=(clip &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = other.detach();
    }
    return *this;
  }

  ~clip() { reset(); }

  explicit operator bool() const noexcept {
    return value_.object != nullptr && value_.operations != nullptr &&
           value_.operations->struct_size >= sizeof(avs_cx_clip_ops_v1) &&
           value_.operations->abi_version == AVS_CX_ABI_VERSION_1 &&
           value_.operations->retain != nullptr && value_.operations->release != nullptr;
  }

  avs_cx_status get_video_info(avs_cx_video_info_v1 *result,
                               avs_cx_error_v1 *error = nullptr) const noexcept {
    if (!valid_ops() || result == nullptr || value_.operations->get_video_info == nullptr) {
      set_error(error, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid CX clip reference");
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    }
    *result = {};
    result->struct_size = sizeof(*result);
    return value_.operations->get_video_info(value_.object, result, error);
  }

  avs_cx_status get_frame(int64_t frame_number, const avs_cx_call_context_v1 *call, frame *result,
                          avs_cx_error_v1 *error = nullptr) const noexcept {
    if (!valid_ops() || result == nullptr || value_.operations->get_frame == nullptr) {
      set_error(error, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid CX clip reference");
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    }
    avs_cx_frame_ref_v1 raw{};
    const avs_cx_status status =
        value_.operations->get_frame(value_.object, frame_number, call, &raw, error);
    if (status == AVS_CX_STATUS_OK) {
      *result = frame::adopt(raw);
    } else if (raw.object != nullptr && raw.operations != nullptr &&
               raw.operations->release != nullptr) {
      raw.operations->release(raw.object);
    }
    return status;
  }

  avs_cx_status get_audio(void *buffer, int64_t start, int64_t count,
                          const avs_cx_call_context_v1 *call,
                          avs_cx_error_v1 *error = nullptr) const noexcept {
    if (!valid_ops())
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    if (value_.operations->get_audio == nullptr) {
      return AVS_CX_STATUS_UNSUPPORTED;
    }
    return value_.operations->get_audio(value_.object, buffer, start, count, call, error);
  }

  avs_cx_status get_parity(int64_t frame_number, int32_t *result,
                           avs_cx_error_v1 *error = nullptr) const noexcept {
    if (!valid_ops() || result == nullptr)
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    if (value_.operations->get_parity == nullptr) {
      return AVS_CX_STATUS_UNSUPPORTED;
    }
    return value_.operations->get_parity(value_.object, frame_number, result, error);
  }

  avs_cx_status set_cache_hints(int32_t hints, int32_t range, int32_t *result,
                                avs_cx_error_v1 *error = nullptr) const noexcept {
    if (!valid_ops() || result == nullptr)
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    if (value_.operations->set_cache_hints == nullptr) {
      return AVS_CX_STATUS_UNSUPPORTED;
    }
    return value_.operations->set_cache_hints(value_.object, hints, range, result, error);
  }

  avs_cx_clip_ref_v1 get() const noexcept { return value_; }

  avs_cx_clip_ref_v1 detach() noexcept {
    const avs_cx_clip_ref_v1 result = value_;
    value_ = {};
    return result;
  }

  void reset() noexcept {
    if (value_.object != nullptr && value_.operations != nullptr &&
        value_.operations->release != nullptr) {
      value_.operations->release(value_.object);
    }
    value_ = {};
  }

private:
  explicit clip(avs_cx_clip_ref_v1 value, bool add_reference) noexcept : value_(value) {
    if (add_reference) {
      retain_value();
    }
  }

  bool valid_ops() const noexcept { return static_cast<bool>(*this); }

  void retain_value() noexcept {
    if (value_.object != nullptr && value_.operations != nullptr &&
        value_.operations->retain != nullptr) {
      value_.operations->retain(value_.object);
    }
  }

  avs_cx_clip_ref_v1 value_{};
};

class runtime {
public:
  runtime() noexcept = default;

  avs_cx_status initialize(const avs_cx_host_v1 *host) noexcept {
    host_ = nullptr;
    registry_ = nullptr;
    environment_ = nullptr;
    clip_feature_ = nullptr;
    frame_feature_ = nullptr;

    if (host == nullptr || host->abi_version != AVS_CX_ABI_VERSION_1 ||
        host->struct_size < sizeof(avs_cx_host_v1) || host->query_feature == nullptr) {
      return AVS_CX_STATUS_ABI_MISMATCH;
    }
    host_ = host;

    avs_cx_status status = query(AVS_CX_FEATURE_REGISTRY, AVS_CX_ABI_VERSION_1, &registry_);
    if (status != AVS_CX_STATUS_OK)
      return status;
    status = query(AVS_CX_FEATURE_ENVIRONMENT, AVS_CX_ABI_VERSION_1, &environment_);
    if (status != AVS_CX_STATUS_OK)
      return status;
    status = query(AVS_CX_FEATURE_CLIP, AVS_CX_ABI_VERSION_1, &clip_feature_);
    if (status != AVS_CX_STATUS_OK)
      return status;
    if (clip_feature_->clip_ops_version != AVS_CX_ABI_VERSION_1 ||
        clip_feature_->clip_ref_size != sizeof(avs_cx_clip_ref_v1)) {
      return AVS_CX_STATUS_ABI_MISMATCH;
    }
    status = query(AVS_CX_FEATURE_FRAME, AVS_CX_ABI_VERSION_1, &frame_feature_);
    if (status != AVS_CX_STATUS_OK)
      return status;
    if (frame_feature_->frame_ops_version != AVS_CX_ABI_VERSION_1 ||
        frame_feature_->frame_ref_size != sizeof(avs_cx_frame_ref_v1)) {
      return AVS_CX_STATUS_ABI_MISMATCH;
    }
    return AVS_CX_STATUS_OK;
  }

  avs_cx_status set_plugin_name(const char *name) const noexcept {
    if (registry_ == nullptr || registry_->set_plugin_name == nullptr) {
      return AVS_CX_STATUS_ABI_MISMATCH;
    }
    const avs_cx_string_view_v1 name_view = string_view(name);
    return registry_->set_plugin_name(registry_->context, &name_view);
  }

  avs_cx_status register_function(const char *name, const char *parameters,
                                  avs_cx_apply_function_v1 apply, void *user_data) const noexcept {
    if (registry_ == nullptr || registry_->register_function == nullptr) {
      return AVS_CX_STATUS_ABI_MISMATCH;
    }
    const avs_cx_string_view_v1 name_view = string_view(name);
    const avs_cx_string_view_v1 parameters_view = string_view(parameters);
    return registry_->register_function(registry_->context, &name_view, &parameters_view, apply,
                                        user_data);
  }

  avs_cx_status register_shutdown(avs_cx_shutdown_v1 shutdown, void *user_data) const noexcept {
    if (registry_ == nullptr || registry_->register_shutdown == nullptr) {
      return AVS_CX_STATUS_ABI_MISMATCH;
    }
    return registry_->register_shutdown(registry_->context, shutdown, user_data);
  }

  avs_cx_status make_writable(const avs_cx_call_context_v1 *call, frame *value,
                              avs_cx_error_v1 *error = nullptr) const noexcept {
    if (call == nullptr || call->struct_size < sizeof(*call) ||
        call->abi_version != AVS_CX_ABI_VERSION_1 || call->host != host_ ||
        call->environment == nullptr || value == nullptr || !*value || environment_ == nullptr ||
        environment_->make_writable == nullptr) {
      set_error(error, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid CX call context");
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    }
    // The in/out reference owns exactly one reference before and after the
    // call.
    avs_cx_frame_ref_v1 raw = value->detach();
    const avs_cx_status status =
        environment_->make_writable(environment_->context, call->environment, &raw, error);
    *value = frame::adopt(raw);
    return status;
  }

  avs_cx_status get_cpu_flags(const avs_cx_call_context_v1 *call, uint64_t *flags) const noexcept {
    if (call == nullptr || call->struct_size < sizeof(*call) ||
        call->abi_version != AVS_CX_ABI_VERSION_1 || call->host != host_ ||
        call->environment == nullptr || flags == nullptr || environment_ == nullptr ||
        environment_->get_cpu_flags == nullptr) {
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    }
    return environment_->get_cpu_flags(environment_->context, call->environment, flags);
  }

  const avs_cx_host_v1 *host() const noexcept { return host_; }

private:
  template <typename T>
  avs_cx_status query(uint64_t key, uint32_t version, const T **result) const noexcept {
    const void *feature = nullptr;
    const avs_cx_status status =
        host_->query_feature(host_->host_context, key, version, sizeof(T), &feature);
    if (status != AVS_CX_STATUS_OK) {
      return status;
    }
    const T *typed = static_cast<const T *>(feature);
    if (typed == nullptr || typed->struct_size < sizeof(T) || typed->abi_version != version) {
      return AVS_CX_STATUS_ABI_MISMATCH;
    }
    *result = typed;
    return AVS_CX_STATUS_OK;
  }

  const avs_cx_host_v1 *host_{};
  const avs_cx_registry_feature_v1 *registry_{};
  const avs_cx_environment_feature_v1 *environment_{};
  const avs_cx_clip_feature_v1 *clip_feature_{};
  const avs_cx_frame_feature_v1 *frame_feature_{};
};

inline bool has_feature(const avs_cx_host_v1 *host, uint64_t key, uint32_t exact_version,
                        uint32_t minimum_struct_size) noexcept {
  if (host == nullptr || host->struct_size < sizeof(*host) ||
      host->abi_version != AVS_CX_ABI_VERSION_1 || host->query_feature == nullptr) {
    return false;
  }
  const void *feature = nullptr;
  return host->query_feature(host->host_context, key, exact_version, minimum_struct_size,
                             &feature) == AVS_CX_STATUS_OK &&
         feature != nullptr;
}

static_assert(sizeof(avs_cx_value_payload_v1) == 16,
              "CX value payload must remain compiler-neutral");
static_assert(offsetof(avs_cx_value_v1, value) == 8, "CX value payload offset is part of the ABI");
static_assert(sizeof(avs_cx_value_v1) == 40, "CX value size is part of the ABI");
static_assert(sizeof(avs_cx_video_info_v1) == 128, "CX video-info size is part of the ABI");
static_assert(sizeof(avs_cx_frame_ref_v1) == sizeof(void *) * 2,
              "CX frame refs contain exactly two pointers");
static_assert(sizeof(avs_cx_clip_ref_v1) == sizeof(void *) * 2,
              "CX clip refs contain exactly two pointers");
static_assert(sizeof(avs_cx_host_v1) == 8 + sizeof(void *) * 6,
              "CX host table layout is part of the ABI");
static_assert(sizeof(avs_cx_call_context_v1) == 8 + sizeof(void *) * 6,
              "CX call-context layout is part of the ABI");
static_assert(sizeof(avs_cx_registry_feature_v1) == 8 + sizeof(void *) * 12,
              "CX registry table layout is part of the ABI");
static_assert(sizeof(avs_cx_environment_feature_v1) == 8 + sizeof(void *) * 11,
              "CX environment table layout is part of the ABI");
static_assert(sizeof(avs_cx_clip_ops_v1) == 8 + sizeof(void *) * 15,
              "CX clip operations layout is part of the ABI");
static_assert(sizeof(avs_cx_frame_ops_v1) == 8 + sizeof(void *) * 12,
              "CX frame operations layout is part of the ABI");
static_assert(offsetof(avs_cx_host_v1, host_context) == 8,
              "CX host context offset is part of the ABI");
static_assert(offsetof(avs_cx_registry_feature_v1, context) == 8,
              "CX registry context offset is part of the ABI");

} // namespace cx
} // namespace avs

#endif // AVS_CX_SDK_RUNTIME_H
