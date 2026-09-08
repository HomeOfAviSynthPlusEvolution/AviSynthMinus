#include "CxPlugin.h"

#include "../InternalEnvironment.h"
#include "../PluginManager.h"

#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kCxAbiVersion = AVS_CX_ABI_VERSION_1;

struct CxFunctionBinding {
  CxHostSession *session;
  avs_cx_apply_function_v1 apply;
  void *plugin_user_data;
};

std::string CopyStringView(avs_cx_string_view_v1 value) {
  if (value.data == nullptr) {
    if (value.size == 0)
      return {};
    throw std::invalid_argument("null CX string data");
  }
  return std::string(value.data, value.size);
}

bool ContainsNul(const std::string &value) { return value.find('\0') != std::string::npos; }

void SetCxError(avs_cx_error_v1 *error, avs_cx_status status, const char *message) noexcept {
  if (error == nullptr || error->struct_size < sizeof(*error))
    return;
  *error = {};
  error->struct_size = sizeof(*error);
  error->code = status;
  error->message.data = message;
  error->message.size = message == nullptr ? 0U : static_cast<uint32_t>(std::strlen(message));
}

void SetCxErrorCopy(avs_cx_error_v1 *error, avs_cx_status status, const char *message) noexcept {
  static thread_local std::string storage;
  try {
    storage = message == nullptr ? "unknown CX error" : message;
    SetCxError(error, status, storage.c_str());
  } catch (...) {
    SetCxError(error, status, "unable to preserve CX error text");
  }
}

std::string ErrorMessage(avs_cx_status status, const avs_cx_error_v1 &error) {
  if (error.message.data != nullptr && error.message.size != 0) {
    return std::string(error.message.data, error.message.size);
  }
  return "CX operation failed with status " + std::to_string(status);
}

void VideoInfoToCx(const VideoInfo &source, avs_cx_video_info_v1 *destination) {
  *destination = {};
  destination->struct_size = sizeof(*destination);
  destination->width = source.width;
  destination->height = source.height;
  destination->fps_numerator = source.fps_numerator;
  destination->fps_denominator = source.fps_denominator;
  destination->num_frames = source.num_frames;
  destination->pixel_type = static_cast<uint32_t>(source.pixel_type);
  destination->audio_samples_per_second = source.audio_samples_per_second;
  destination->sample_type = source.sample_type;
  destination->channels = source.nchannels;
  destination->num_audio_samples = source.num_audio_samples;
  destination->image_type = static_cast<uint32_t>(source.image_type);
}

VideoInfo VideoInfoFromCx(const avs_cx_video_info_v1 &source) {
  if (source.struct_size < sizeof(avs_cx_video_info_v1) ||
      source.num_frames < std::numeric_limits<int>::min() ||
      source.num_frames > std::numeric_limits<int>::max()) {
    throw std::invalid_argument("invalid CX video-info descriptor");
  }

  VideoInfo destination{};
  destination.width = source.width;
  destination.height = source.height;
  destination.fps_numerator = source.fps_numerator;
  destination.fps_denominator = source.fps_denominator;
  destination.num_frames = static_cast<int>(source.num_frames);
  destination.pixel_type = static_cast<int>(source.pixel_type);
  destination.audio_samples_per_second = source.audio_samples_per_second;
  destination.sample_type = source.sample_type;
  destination.nchannels = source.channels;
  destination.num_audio_samples = source.num_audio_samples;
  destination.image_type = static_cast<int>(source.image_type);
  return destination;
}

const avs_cx_frame_ops_v1 &HostFrameOperations();

void AVS_CX_CALL HostFrameRetain(void *object) {
  CxCoreFrameAccess::AddRef(static_cast<VideoFrame *>(object));
}

void AVS_CX_CALL HostFrameRelease(void *object) {
  CxCoreFrameAccess::Release(static_cast<VideoFrame *>(object));
}

avs_cx_status AVS_CX_CALL HostFrameGetPlane(void *object, uint32_t plane, uint32_t access,
                                            avs_cx_plane_v1 *plane_out,
                                            avs_cx_error_v1 *error_out) noexcept {
  if (object == nullptr || plane_out == nullptr || plane_out->struct_size < sizeof(*plane_out) ||
      (access != AVS_CX_FRAME_ACCESS_READ && access != AVS_CX_FRAME_ACCESS_WRITE)) {
    SetCxError(error_out, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid frame plane request");
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  *plane_out = {};
  plane_out->struct_size = sizeof(*plane_out);

  try {
    auto *frame = static_cast<VideoFrame *>(object);
    BYTE *data = nullptr;
    if (access == AVS_CX_FRAME_ACCESS_WRITE) {
      data = frame->GetWritePtr(static_cast<int>(plane));
      if (data == nullptr) {
        // A failed write probe is normal, and the legacy API returns nullptr.
        return AVS_CX_STATUS_OK;
      }
    } else {
      data = const_cast<BYTE *>(frame->GetReadPtr(static_cast<int>(plane)));
      if (data == nullptr) {
        SetCxError(error_out, AVS_CX_STATUS_INVALID_ARGUMENT,
                   "requested plane is not present in this frame");
        return AVS_CX_STATUS_INVALID_ARGUMENT;
      }
    }

    plane_out->flags = access;
    plane_out->data = data;
    plane_out->pitch = frame->GetPitch(static_cast<int>(plane));
    plane_out->row_size = frame->GetRowSize(static_cast<int>(plane));
    plane_out->height = frame->GetHeight(static_cast<int>(plane));
    return AVS_CX_STATUS_OK;
  } catch (const AvisynthError &error) {
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.msg);
  } catch (const std::exception &error) {
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.what());
  } catch (...) {
    SetCxError(error_out, AVS_CX_STATUS_HOST_ERROR, "unknown host frame error");
  }
  return AVS_CX_STATUS_HOST_ERROR;
}

avs_cx_status AVS_CX_CALL HostFrameGetPixelType(void *object, uint32_t *pixel_type_out) noexcept {
  if (object == nullptr || pixel_type_out == nullptr) {
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  *pixel_type_out = static_cast<uint32_t>(static_cast<VideoFrame *>(object)->GetPixelType());
  return AVS_CX_STATUS_OK;
}

const avs_cx_frame_ops_v1 &HostFrameOperations() {
  static const avs_cx_frame_ops_v1 operations = [] {
    avs_cx_frame_ops_v1 result{};
    result.struct_size = sizeof(result);
    result.abi_version = kCxAbiVersion;
    result.retain = &HostFrameRetain;
    result.release = &HostFrameRelease;
    result.get_plane = &HostFrameGetPlane;
    result.get_pixel_type = &HostFrameGetPixelType;
    return result;
  }();
  return operations;
}

// The opaque token is the core's IClip address. Its ownership stays in the
// core; canonical tokens avoid one new identity per value conversion.
void AVS_CX_CALL HostClipRetain(void *object) {
  CxCoreFrameAccess::AddRef(static_cast<IClip *>(object));
}
void AVS_CX_CALL HostClipRelease(void *object) {
  CxCoreFrameAccess::Release(static_cast<IClip *>(object));
}

bool ValidCall(const avs_cx_call_context_v1 *call) {
  return call != nullptr && call->struct_size >= sizeof(avs_cx_call_context_v1) &&
         call->abi_version == kCxAbiVersion && call->environment != nullptr;
}

avs_cx_status AVS_CX_CALL HostClipGetVideoInfo(void *object, avs_cx_video_info_v1 *video_info_out,
                                               avs_cx_error_v1 *error_out) noexcept {
  if (object == nullptr || video_info_out == nullptr ||
      video_info_out->struct_size < sizeof(*video_info_out)) {
    SetCxError(error_out, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid clip video-info request");
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  *video_info_out = {};
  video_info_out->struct_size = sizeof(*video_info_out);
  try {
    VideoInfoToCx(static_cast<IClip *>(object)->GetVideoInfo(), video_info_out);
    return AVS_CX_STATUS_OK;
  } catch (const AvisynthError &error) {
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.msg);
  } catch (const std::exception &error) {
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.what());
  } catch (...) {
    SetCxError(error_out, AVS_CX_STATUS_HOST_ERROR, "unknown host clip error");
  }
  return AVS_CX_STATUS_HOST_ERROR;
}

avs_cx_status AVS_CX_CALL HostClipGetFrame(void *object, int64_t frame_number,
                                           const avs_cx_call_context_v1 *call,
                                           avs_cx_frame_ref_v1 *frame_out,
                                           avs_cx_error_v1 *error_out) noexcept {
  if (object == nullptr || frame_out == nullptr || !ValidCall(call) ||
      frame_number < std::numeric_limits<int>::min() ||
      frame_number > std::numeric_limits<int>::max()) {
    SetCxError(error_out, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid clip frame request");
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  *frame_out = {};
  try {
    auto *environment = static_cast<IScriptEnvironment *>(call->environment);
    PVideoFrame frame = static_cast<IClip *>(object)->GetFrame(
        static_cast<int>(frame_number), environment);
    VideoFrame *raw = frame.operator->();
    if (raw == nullptr) {
      SetCxError(error_out, AVS_CX_STATUS_HOST_ERROR, "host clip returned a null frame");
      return AVS_CX_STATUS_HOST_ERROR;
    }
    frame_out->object = CxCoreFrameAccess::Detach(&frame);
    frame_out->operations = &HostFrameOperations();
    return AVS_CX_STATUS_OK;
  } catch (const AvisynthError &error) {
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.msg);
  } catch (const std::exception &error) {
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.what());
  } catch (...) {
    SetCxError(error_out, AVS_CX_STATUS_HOST_ERROR, "unknown host clip error");
  }
  return AVS_CX_STATUS_HOST_ERROR;
}

avs_cx_status AVS_CX_CALL HostClipGetAudio(void *object, void *buffer, int64_t start, int64_t count,
                                           const avs_cx_call_context_v1 *call,
                                           avs_cx_error_v1 *error_out) noexcept {
  if (object == nullptr || !ValidCall(call)) {
    SetCxError(error_out, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid clip audio request");
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  try {
    static_cast<IClip *>(object)->GetAudio(
        buffer, start, count, static_cast<IScriptEnvironment *>(call->environment));
    return AVS_CX_STATUS_OK;
  } catch (const AvisynthError &error) {
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.msg);
  } catch (const std::exception &error) {
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.what());
  } catch (...) {
    SetCxError(error_out, AVS_CX_STATUS_HOST_ERROR, "unknown host clip error");
  }
  return AVS_CX_STATUS_HOST_ERROR;
}

avs_cx_status AVS_CX_CALL HostClipGetParity(void *object, int64_t frame_number, int32_t *parity_out,
                                            avs_cx_error_v1 *error_out) noexcept {
  if (object == nullptr || parity_out == nullptr ||
      frame_number < std::numeric_limits<int>::min() ||
      frame_number > std::numeric_limits<int>::max()) {
    SetCxError(error_out, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid clip parity request");
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  try {
    *parity_out =
        static_cast<IClip *>(object)->GetParity(static_cast<int>(frame_number)) ? 1
                                                                                               : 0;
    return AVS_CX_STATUS_OK;
  } catch (...) {
    SetCxError(error_out, AVS_CX_STATUS_HOST_ERROR, "host clip parity request failed");
    return AVS_CX_STATUS_HOST_ERROR;
  }
}

avs_cx_status AVS_CX_CALL HostClipSetCacheHints(void *object, int32_t cache_hints,
                                                int32_t frame_range, int32_t *result_out,
                                                avs_cx_error_v1 *error_out) noexcept {
  if (object == nullptr || result_out == nullptr) {
    SetCxError(error_out, AVS_CX_STATUS_INVALID_ARGUMENT, "invalid cache-hint request");
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  try {
    *result_out =
        static_cast<IClip *>(object)->SetCacheHints(cache_hints, frame_range);
    return AVS_CX_STATUS_OK;
  } catch (...) {
    SetCxError(error_out, AVS_CX_STATUS_HOST_ERROR, "host cache-hint request failed");
    return AVS_CX_STATUS_HOST_ERROR;
  }
}

const avs_cx_clip_ops_v1 &HostClipOperations() {
  static const avs_cx_clip_ops_v1 operations = [] {
    avs_cx_clip_ops_v1 result{};
    result.struct_size = sizeof(result);
    result.abi_version = kCxAbiVersion;
    result.retain = &HostClipRetain;
    result.release = &HostClipRelease;
    result.get_video_info = &HostClipGetVideoInfo;
    result.get_frame = &HostClipGetFrame;
    result.get_audio = &HostClipGetAudio;
    result.get_parity = &HostClipGetParity;
    result.set_cache_hints = &HostClipSetCacheHints;
    return result;
  }();
  return operations;
}

class CxArgumentStorage {
public:
  ~CxArgumentStorage() {
    for (IClip *clip : clips_) {
      HostClipRelease(clip);
    }
  }

  avs_cx_value_v1 Convert(const AVSValue &source) {
    avs_cx_value_v1 result{};
    result.struct_size = sizeof(result);

    if (!source.Defined()) {
      result.type = AVS_CX_VALUE_UNDEFINED;
    } else if (source.IsBool()) {
      result.type = AVS_CX_VALUE_BOOL;
      result.value.boolean = source.AsBool() ? 1 : 0;
    } else if (source.IsInt()) {
      result.type = source.IsLongStrict() ? AVS_CX_VALUE_INT : AVS_CX_VALUE_INT32;
      result.value.integer = source.AsLong();
    } else if (source.IsFloat()) {
      result.type = source.IsFloatfStrict() ? AVS_CX_VALUE_FLOAT32 : AVS_CX_VALUE_FLOAT;
      result.value.floating_point = source.AsFloat();
    } else if (source.IsString()) {
      result.type = AVS_CX_VALUE_STRING;
      const char *value = source.AsString();
      result.value.string.data = value;
      result.value.string.size = value == nullptr ? 0U : static_cast<uint32_t>(std::strlen(value));
    } else if (source.IsClip()) {
      if (!source.AsClip())
        return result;
      result.type = AVS_CX_VALUE_CLIP;
      IClip *clip = source.AsClip().operator->();
      clips_.push_back(clip);
      HostClipRetain(clip);
      result.value.clip.object = clip;
      result.value.clip.operations = &HostClipOperations();
    } else if (source.IsArray()) {
      result.type = AVS_CX_VALUE_ARRAY;
      arrays_.emplace_back(static_cast<size_t>(source.ArraySize()));
      auto &values = arrays_.back();
      for (int i = 0; i < source.ArraySize(); ++i) {
        values[static_cast<size_t>(i)] = Convert(source[i]);
      }
      result.value.array.values = values.data();
      result.value.array.size = static_cast<uint32_t>(values.size());
    } else {
      throw std::invalid_argument("unsupported AVSValue type at CX boundary");
    }
    return result;
  }

private:
  std::vector<IClip *> clips_;
  std::deque<std::vector<avs_cx_value_v1>> arrays_;
};

struct SdkResultOwner {
  AVSValue value;
  CxArgumentStorage storage;
};

void ReleaseCxValue(const avs_cx_value_v1 &value) noexcept {
  if (value.type == AVS_CX_VALUE_CLIP && value.value.clip.object != nullptr &&
      value.value.clip.operations != nullptr && value.value.clip.operations->release != nullptr) {
    value.value.clip.operations->release(value.value.clip.object);
  } else if (value.type == AVS_CX_VALUE_ARRAY && value.value.array.values != nullptr) {
    for (uint32_t i = 0; i < value.value.array.size; ++i) {
      ReleaseCxValue(value.value.array.values[i]);
    }
  }
}

class CxClipProxy final : public IClip {
public:
  CxClipProxy(avs_cx_clip_ref_v1 clip, CxHostSession *session,
              IScriptEnvironment *owner_environment)
      : clip_(clip), session_(session), owner_environment_(owner_environment) {
    if (clip_.object == nullptr || clip_.operations == nullptr ||
        clip_.operations->struct_size < sizeof(avs_cx_clip_ops_v1) ||
        clip_.operations->abi_version != kCxAbiVersion || clip_.operations->retain == nullptr ||
        clip_.operations->release == nullptr || clip_.operations->get_video_info == nullptr ||
        clip_.operations->get_frame == nullptr) {
      throw AvisynthError(owner_environment_->SaveString("invalid clip returned by CX plugin"));
    }
    clip_.operations->retain(clip_.object);

    avs_cx_video_info_v1 cx_info{};
    cx_info.struct_size = sizeof(cx_info);
    avs_cx_error_v1 error{};
    error.struct_size = sizeof(error);
    const avs_cx_status status = clip_.operations->get_video_info(clip_.object, &cx_info, &error);
    if (status != AVS_CX_STATUS_OK) {
      clip_.operations->release(clip_.object);
      clip_ = {};
      const std::string message = ErrorMessage(status, error);
      throw AvisynthError(owner_environment_->SaveString(message.c_str()));
    }
    try {
      video_info_ = VideoInfoFromCx(cx_info);
    } catch (const std::exception &exception) {
      clip_.operations->release(clip_.object);
      clip_ = {};
      throw AvisynthError(owner_environment_->SaveString(exception.what()));
    }
  }

  ~CxClipProxy() override {
    if (clip_.object != nullptr && clip_.operations != nullptr) {
      clip_.operations->release(clip_.object);
    }
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *environment) override {
    avs_cx_call_context_v1 call{};
    call.struct_size = sizeof(call);
    call.abi_version = kCxAbiVersion;
    call.host = session_->Host();
    call.environment = environment;

    avs_cx_frame_ref_v1 frame{};
    avs_cx_error_v1 error{};
    error.struct_size = sizeof(error);
    const avs_cx_status status =
        clip_.operations->get_frame(clip_.object, n, &call, &frame, &error);
    if (status != AVS_CX_STATUS_OK) {
      if (frame.object != nullptr && frame.operations != nullptr &&
          frame.operations->release != nullptr) {
        frame.operations->release(frame.object);
      }
      const std::string message = ErrorMessage(status, error);
      environment->ThrowError("CX plugin GetFrame failed: %s", message.c_str());
      return {};
    }
    if (frame.object == nullptr || frame.operations != &HostFrameOperations()) {
      if (frame.object != nullptr && frame.operations != nullptr &&
          frame.operations->release != nullptr) {
        frame.operations->release(frame.object);
      }
      environment->ThrowError("CX plugin returned a frame not owned by this host");
      return {};
    }

    PVideoFrame result;
    CxCoreFrameAccess::Adopt(&result, static_cast<VideoFrame *>(frame.object));
    return result;
  }

  bool __stdcall GetParity(int n) override {
    if (clip_.operations->get_parity == nullptr)
      return false;
    int32_t result = 0;
    avs_cx_error_v1 error{};
    error.struct_size = sizeof(error);
    const avs_cx_status status = clip_.operations->get_parity(clip_.object, n, &result, &error);
    if (status != AVS_CX_STATUS_OK) {
      const std::string message = ErrorMessage(status, error);
      owner_environment_->ThrowError("CX plugin GetParity failed: %s", message.c_str());
    }
    return result != 0;
  }

  void __stdcall GetAudio(void *buffer, int64_t start, int64_t count,
                          IScriptEnvironment *environment) override {
    if (clip_.operations->get_audio == nullptr) {
      environment->ThrowError("CX plugin does not implement GetAudio");
      return;
    }
    avs_cx_call_context_v1 call{};
    call.struct_size = sizeof(call);
    call.abi_version = kCxAbiVersion;
    call.host = session_->Host();
    call.environment = environment;
    avs_cx_error_v1 error{};
    error.struct_size = sizeof(error);
    const avs_cx_status status =
        clip_.operations->get_audio(clip_.object, buffer, start, count, &call, &error);
    if (status != AVS_CX_STATUS_OK) {
      const std::string message = ErrorMessage(status, error);
      environment->ThrowError("CX plugin GetAudio failed: %s", message.c_str());
    }
  }

  int __stdcall SetCacheHints(int cache_hints, int frame_range) override {
    if (clip_.operations->set_cache_hints == nullptr)
      return 0;
    int32_t result = 0;
    avs_cx_error_v1 error{};
    error.struct_size = sizeof(error);
    const avs_cx_status status =
        clip_.operations->set_cache_hints(clip_.object, cache_hints, frame_range, &result, &error);
    if (status != AVS_CX_STATUS_OK && status != AVS_CX_STATUS_UNSUPPORTED) {
      const std::string message = ErrorMessage(status, error);
      owner_environment_->ThrowError("CX plugin SetCacheHints failed: %s", message.c_str());
    }
    return result;
  }

  const VideoInfo &__stdcall GetVideoInfo() override { return video_info_; }

private:
  avs_cx_clip_ref_v1 clip_{};
  CxHostSession *session_;
  IScriptEnvironment *owner_environment_;
  VideoInfo video_info_{};
};

AVSValue ValueFromCx(const avs_cx_value_v1 &value, CxHostSession *session,
                     IScriptEnvironment *environment) {
  if (value.struct_size < sizeof(avs_cx_value_v1)) {
    environment->ThrowError("CX plugin returned a truncated value descriptor");
  }
  switch (value.type) {
  case AVS_CX_VALUE_UNDEFINED:
    return {};
  case AVS_CX_VALUE_BOOL:
    return AVSValue(value.value.boolean != 0);
  case AVS_CX_VALUE_INT:
    return AVSValue(value.value.integer);
  case AVS_CX_VALUE_INT32:
    return AVSValue(static_cast<int>(value.value.integer));
  case AVS_CX_VALUE_FLOAT:
    return AVSValue(value.value.floating_point);
  case AVS_CX_VALUE_FLOAT32:
    return AVSValue(static_cast<float>(value.value.floating_point));
  case AVS_CX_VALUE_STRING: {
    const std::string text = CopyStringView(value.value.string);
    if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
      environment->ThrowError("CX plugin returned a string that is too large");
    }
    return AVSValue(environment->SaveString(text.c_str(), static_cast<int>(text.size())));
  }
  case AVS_CX_VALUE_CLIP:
    if (value.value.clip.operations == &HostClipOperations() && value.value.clip.object)
      return AVSValue(static_cast<IClip *>(value.value.clip.object));
    return AVSValue(new CxClipProxy(value.value.clip, session, environment));
  case AVS_CX_VALUE_ARRAY: {
    if (value.value.array.values == nullptr && value.value.array.size != 0) {
      environment->ThrowError("CX plugin returned an invalid array descriptor");
    }
    if (value.value.array.size > static_cast<uint32_t>(std::numeric_limits<short>::max())) {
      environment->ThrowError("CX plugin returned an array that is too large");
    }
    std::vector<AVSValue> values;
    values.reserve(value.value.array.size);
    for (uint32_t i = 0; i < value.value.array.size; ++i) {
      values.push_back(ValueFromCx(value.value.array.values[i], session, environment));
    }
    return AVSValue(values.data(), static_cast<int>(values.size()));
  }
  default:
    environment->ThrowError("CX plugin returned an unknown value type %u", value.type);
    return {};
  }
}

} // namespace

void CxCoreFrameAccess::AddRef(IClip *clip) { clip->AddRef(); }
void CxCoreFrameAccess::Release(IClip *clip) { clip->Release(); }

void CxCoreFrameAccess::AddRef(VideoFrame *frame) {
  if (frame != nullptr)
    frame->AddRef();
}

void CxCoreFrameAccess::Release(VideoFrame *frame) {
  if (frame != nullptr)
    frame->Release();
}

void CxCoreFrameAccess::Adopt(PVideoFrame *destination, VideoFrame *frame) noexcept {
  assert(destination != nullptr && destination->p == nullptr);
  destination->p = frame;
}

VideoFrame *CxCoreFrameAccess::Detach(PVideoFrame *source) noexcept {
  assert(source != nullptr);
  VideoFrame *const result = source->p;
  source->p = nullptr;
  return result;
}

CxHostSession::CxHostSession(PluginManager *manager, InternalEnvironment *environment)
    : manager_(manager), environment_(environment) {
  host_.struct_size = sizeof(host_);
  host_.abi_version = kCxAbiVersion;
  host_.host_context = this;
  host_.query_feature = &QueryFeature;

  registry_.struct_size = sizeof(registry_);
  registry_.abi_version = kCxAbiVersion;
  registry_.context = this;
  registry_.set_plugin_name = &SetPluginName;
  registry_.register_function = &RegisterFunction;
  registry_.register_shutdown = &RegisterShutdown;

  environment_feature_.struct_size = sizeof(environment_feature_);
  environment_feature_.abi_version = kCxAbiVersion;
  environment_feature_.context = this;
  environment_feature_.make_writable = &MakeWritable;
  environment_feature_.get_cpu_flags = &GetCpuFlags;

  clip_feature_.struct_size = sizeof(clip_feature_);
  clip_feature_.abi_version = kCxAbiVersion;
  clip_feature_.clip_ops_version = kCxAbiVersion;
  clip_feature_.clip_ref_size = sizeof(avs_cx_clip_ref_v1);

  frame_feature_.struct_size = sizeof(frame_feature_);
  frame_feature_.abi_version = kCxAbiVersion;
  frame_feature_.frame_ops_version = kCxAbiVersion;
  frame_feature_.frame_ref_size = sizeof(avs_cx_frame_ref_v1);
  sdk_feature_.struct_size = sizeof(sdk_feature_);
  sdk_feature_.abi_version = kCxAbiVersion;
  sdk_feature_.context = this;
  sdk_feature_.initialization_environment = static_cast<IScriptEnvironment *>(environment);
  sdk_feature_.dispatch = &SdkDispatch;
  sdk_feature_.release_result = [](void *p) { delete static_cast<SdkResultOwner *>(p); };
  sdk_feature_.frame_metadata = [](const avs_cx_frame_ref_v1 *ref, uint32_t operation,
                                   int32_t *value, avs_cx_error_v1 *error) -> avs_cx_status {
    if (!ref || !ref->object || ref->operations != &HostFrameOperations() || !value)
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    auto *frame = static_cast<VideoFrame *>(ref->object);
    try {
      if (operation == 0)
        *value = frame->IsWritable();
      else if (operation == 1)
        *value = frame->IsPropertyWritable();
      else if (operation == 2) {
        if (!frame->IsWritable())
          return AVS_CX_STATUS_INVALID_ARGUMENT;
        frame->AmendPixelType(*value);
      } else
        return AVS_CX_STATUS_UNSUPPORTED;
      return AVS_CX_STATUS_OK;
    } catch (...) {
      SetCxError(error, AVS_CX_STATUS_HOST_ERROR, "CX frame metadata failed");
      return AVS_CX_STATUS_HOST_ERROR;
    }
  };
}

CxHostSession::~CxHostSession() {
  if (committed_)
    return;
  RemoveFunctions();
  for (auto it = pending_shutdowns_.rbegin(); it != pending_shutdowns_.rend(); ++it) {
    try {
      it->shutdown(it->plugin_user_data);
    } catch (...) {
    }
  }
}

// Remove only this session's registrations, including aliases. Nested plugin
// loads may have registered unrelated functions which must survive rollback.
void CxHostSession::RemoveFunctions(void *binding) noexcept {
  for (auto *map : {&manager_->ExternalFunctions, &manager_->AutoloadedFunctions}) {
    for (;;) {
      const AVSFunction *found = nullptr;
      for (const auto &entry : *map) {
        for (const auto *f : entry.second) {
          if (f->apply == &ApplyBridge &&
              static_cast<CxFunctionBinding *>(f->user_data)->session == this &&
              (!binding || f->user_data == binding)) { found = f; break; }
        }
        if (found) break;
      }
      if (!found) break;
      for (auto it = map->begin(); it != map->end();) {
        auto &list = it->second;
        list.erase(std::remove(list.begin(), list.end(), found), list.end());
        if (list.empty()) it = map->erase(it); else ++it;
      }
      delete found;
    }
  }
}

avs_cx_status CxHostSession::Commit() noexcept {
  if (committed_) return AVS_CX_STATUS_OK;
  try {
    environment_->AtExit(&ShutdownBridge, this);
    committed_ = true;
    return AVS_CX_STATUS_OK;
  } catch (const AvisynthError &error) {
    SetLastError(error.msg);
  } catch (const std::exception &error) {
    SetLastError(error.what());
  } catch (...) {
    SetLastError("unable to commit CX registrations");
  }
  return AVS_CX_STATUS_HOST_ERROR;
}

avs_cx_status AVS_CX_CALL CxHostSession::QueryFeature(void *context, uint64_t feature_key,
                                                      uint32_t exact_version,
                                                      uint32_t minimum_struct_size,
                                                      const void **feature_out) noexcept {
  if (feature_out == nullptr) {
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  *feature_out = nullptr;
  if (context == nullptr) {
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  if (exact_version != kCxAbiVersion) {
    return AVS_CX_STATUS_FEATURE_NOT_FOUND;
  }

  auto *self = static_cast<CxHostSession *>(context);
  const void *feature = nullptr;
  uint32_t size = 0;
  switch (feature_key) {
  case AVS_CX_FEATURE_REGISTRY:
    feature = &self->registry_;
    size = sizeof(self->registry_);
    break;
  case AVS_CX_FEATURE_ENVIRONMENT:
    feature = &self->environment_feature_;
    size = sizeof(self->environment_feature_);
    break;
  case AVS_CX_FEATURE_CLIP:
    feature = &self->clip_feature_;
    size = sizeof(self->clip_feature_);
    break;
  case AVS_CX_FEATURE_FRAME:
    feature = &self->frame_feature_;
    size = sizeof(self->frame_feature_);
    break;
  case AVS_CX_FEATURE_SDK:
    feature = &self->sdk_feature_;
    size = sizeof(self->sdk_feature_);
    break;
  default:
    return AVS_CX_STATUS_FEATURE_NOT_FOUND;
  }
  if (minimum_struct_size > size) {
    return AVS_CX_STATUS_FEATURE_NOT_FOUND;
  }
  *feature_out = feature;
  return AVS_CX_STATUS_OK;
}

avs_cx_status AVS_CX_CALL CxHostSession::SetPluginName(void *context,
                                                       const avs_cx_string_view_v1 *name) noexcept {
  if (context == nullptr || name == nullptr)
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  auto *self = static_cast<CxHostSession *>(context);
  try {
    self->plugin_name_ = CopyStringView(*name);
    if (ContainsNul(self->plugin_name_)) {
      self->SetLastError("CX plugin name contains an embedded NUL");
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    }
    return AVS_CX_STATUS_OK;
  } catch (const std::exception &error) {
    self->SetLastError(error.what());
  } catch (...) {
    self->SetLastError("unable to copy CX plugin name");
  }
  return AVS_CX_STATUS_HOST_ERROR;
}

avs_cx_status AVS_CX_CALL CxHostSession::RegisterFunction(
    void *context, const avs_cx_string_view_v1 *name, const avs_cx_string_view_v1 *parameter_string,
    avs_cx_apply_function_v1 apply, void *plugin_user_data) noexcept {
  if (context == nullptr || name == nullptr || parameter_string == nullptr || apply == nullptr) {
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  auto *self = static_cast<CxHostSession *>(context);
  void *stored = nullptr;
  try {
    const std::string function_name = CopyStringView(*name);
    const std::string parameters = CopyStringView(*parameter_string);
    if (function_name.empty() || ContainsNul(function_name) || ContainsNul(parameters) ||
        !PluginManager::IsValidParameterString(parameters.c_str())) {
      self->SetLastError("invalid CX function name or parameter string");
      return AVS_CX_STATUS_INVALID_ARGUMENT;
    }

    const CxFunctionBinding binding{self, apply, plugin_user_data};
    stored = const_cast<char *>(self->environment_->SaveString(
        reinterpret_cast<const char *>(&binding), sizeof(binding)));
    self->manager_->AddFunction(function_name.c_str(), parameters.c_str(),
        &ApplyBridge, stored, nullptr, false, false);
    return AVS_CX_STATUS_OK;
  } catch (const AvisynthError &error) {
    self->SetLastError(error.msg);
  } catch (const std::exception &error) {
    self->SetLastError(error.what());
  } catch (...) {
    self->SetLastError("unknown error while registering CX function");
  }
  if (stored) self->RemoveFunctions(stored);
  return AVS_CX_STATUS_HOST_ERROR;
}

avs_cx_status AVS_CX_CALL CxHostSession::RegisterShutdown(void *context,
                                                          avs_cx_shutdown_v1 shutdown,
                                                          void *plugin_user_data) noexcept {
  if (context == nullptr || shutdown == nullptr)
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  auto *self = static_cast<CxHostSession *>(context);
  try {
    self->pending_shutdowns_.push_back(PendingShutdown{shutdown, plugin_user_data});
    return AVS_CX_STATUS_OK;
  } catch (const AvisynthError &error) {
    self->SetLastError(error.msg);
  } catch (...) {
    self->SetLastError("unable to register CX shutdown callback");
  }
  return AVS_CX_STATUS_HOST_ERROR;
}

avs_cx_status AVS_CX_CALL CxHostSession::MakeWritable(void *, void *environment,
                                                      avs_cx_frame_ref_v1 *frame,
                                                      avs_cx_error_v1 *error_out) noexcept {
  if (environment == nullptr || frame == nullptr || frame->object == nullptr ||
      frame->operations != &HostFrameOperations()) {
    SetCxError(error_out, AVS_CX_STATUS_INVALID_ARGUMENT,
               "MakeWritable requires a frame owned by this host");
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }

  PVideoFrame local;
  CxCoreFrameAccess::Adopt(&local, static_cast<VideoFrame *>(frame->object));
  frame->object = nullptr;

  try {
    auto *env = static_cast<IScriptEnvironment *>(environment);
    env->MakeWritable(&local);
    frame->object = CxCoreFrameAccess::Detach(&local);
    if (frame->object == nullptr) {
      SetCxError(error_out, AVS_CX_STATUS_HOST_ERROR, "MakeWritable returned a null frame");
      return AVS_CX_STATUS_HOST_ERROR;
    }
    frame->operations = &HostFrameOperations();
    return AVS_CX_STATUS_OK;
  } catch (const AvisynthError &error) {
    frame->object = CxCoreFrameAccess::Detach(&local);
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.msg);
  } catch (const std::exception &error) {
    frame->object = CxCoreFrameAccess::Detach(&local);
    SetCxErrorCopy(error_out, AVS_CX_STATUS_HOST_ERROR, error.what());
  } catch (...) {
    frame->object = CxCoreFrameAccess::Detach(&local);
    SetCxError(error_out, AVS_CX_STATUS_HOST_ERROR, "unknown MakeWritable failure");
  }
  return AVS_CX_STATUS_HOST_ERROR;
}

avs_cx_status AVS_CX_CALL CxHostSession::GetCpuFlags(void *, void *environment,
                                                     uint64_t *flags_out) noexcept {
  if (environment == nullptr || flags_out == nullptr) {
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  }
  try {
    *flags_out =
        static_cast<uint32_t>(static_cast<IScriptEnvironment *>(environment)->GetCPUFlags());
    return AVS_CX_STATUS_OK;
  } catch (...) {
    return AVS_CX_STATUS_HOST_ERROR;
  }
}

AVSValue __cdecl CxHostSession::ApplyBridge(AVSValue arguments, void *user_data,
                                            IScriptEnvironment *environment) {
  auto *binding = static_cast<CxFunctionBinding *>(user_data);
  if (binding == nullptr || binding->session == nullptr || binding->apply == nullptr) {
    environment->ThrowError("invalid CX function binding");
    return {};
  }

  CxArgumentStorage storage;
  const int argument_count = arguments.ArraySize();
  std::vector<avs_cx_value_v1> cx_arguments(static_cast<size_t>(argument_count));
  for (int i = 0; i < argument_count; ++i) {
    cx_arguments[static_cast<size_t>(i)] = storage.Convert(arguments[i]);
  }

  avs_cx_call_context_v1 call{};
  call.struct_size = sizeof(call);
  call.abi_version = kCxAbiVersion;
  call.host = binding->session->Host();
  call.environment = environment;

  avs_cx_value_v1 result{};
  result.struct_size = sizeof(result);
  avs_cx_error_v1 error{};
  error.struct_size = sizeof(error);
  const avs_cx_status status =
      binding->apply(binding->plugin_user_data, &call, cx_arguments.data(),
                     static_cast<uint32_t>(cx_arguments.size()), &result, &error);
  if (status != AVS_CX_STATUS_OK) {
    const std::string message = ErrorMessage(status, error);
    ReleaseCxValue(result);
    environment->ThrowError("CX plugin function failed: %s", message.c_str());
    return {};
  }

  try {
    AVSValue converted = ValueFromCx(result, binding->session, environment);
    ReleaseCxValue(result);
    return converted;
  } catch (...) {
    ReleaseCxValue(result);
    throw;
  }
}

void __cdecl CxHostSession::ShutdownBridge(void *user_data, IScriptEnvironment *) {
  auto *session = static_cast<CxHostSession *>(user_data);
  for (auto it = session->pending_shutdowns_.rbegin(); it != session->pending_shutdowns_.rend();
       ++it) {
    try {
      it->shutdown(it->plugin_user_data);
    } catch (...) {
    }
  }
  session->pending_shutdowns_.clear();
}

void CxHostSession::SetLastError(const char *message) noexcept {
  try {
    last_error_ = message == nullptr ? "unknown CX host error" : message;
  } catch (...) {
    last_error_.clear();
  }
}

avs_cx_status AVS_CX_CALL CxHostSession::SdkDispatch(void *context, void *environment,
                                                     const avs_cx_sdk_request_v1 *q,
                                                     avs_cx_sdk_result_v1 *r,
                                                     avs_cx_error_v1 *error) noexcept {
  if (!context || !environment || !q || !r || q->struct_size < sizeof(*q) ||
      r->struct_size < sizeof(*r))
    return AVS_CX_STATUS_INVALID_ARGUMENT;
  *r = {};
  r->struct_size = sizeof(*r);
  auto *env = static_cast<IScriptEnvironment *>(environment);
  auto *session = static_cast<CxHostSession *>(context);
  try {
    auto frame = [](avs_cx_frame_ref_v1 ref) -> PVideoFrame {
      if (!ref.object || ref.operations != &HostFrameOperations())
        throw std::invalid_argument("CX SDK expected host frame");
      return PVideoFrame(static_cast<VideoFrame *>(ref.object));
    };
    auto value = [&] { return q->value ? ValueFromCx(*q->value, session, env) : AVSValue(); };
    auto result = [&](AVSValue v) {
      std::unique_ptr<SdkResultOwner> owner(new SdkResultOwner);
      owner->value = v;
      r->value = owner->storage.Convert(owner->value);
      r->result_owner = owner.release();
    };
    switch (q->operation) {
    case AVS_CX_SDK_SAVE_STRING:
      r->pointer = env->SaveString(q->text, static_cast<int>(q->integer));
      break;
    case AVS_CX_SDK_NEW_FRAME: {
      if (!q->video_info)
        throw std::invalid_argument("CX SDK missing video info");
      const VideoInfo vi = VideoInfoFromCx(*q->video_info);
      PVideoFrame source;
      if (q->frame.object)
        source = frame(q->frame);
      PVideoFrame f =
          env->NewVideoFrameP(vi, source ? &source : nullptr, static_cast<int>(q->integer));
      r->frame = {CxCoreFrameAccess::Detach(&f), &HostFrameOperations()};
      break;
    }
    case AVS_CX_SDK_FRAME_WRITABLE:
      if (!q->frame.object || q->frame.operations != &HostFrameOperations())
        throw std::invalid_argument("CX SDK expected host frame");
      r->integer = static_cast<VideoFrame *>(q->frame.object)->IsWritable();
      break;
    case AVS_CX_SDK_INVOKE:
      result(env->Invoke(q->text, value(), q->names));
      break;
    case AVS_CX_SDK_GET_VAR:
      result(env->GetVar(q->text));
      break;
    case AVS_CX_SDK_SET_VAR:
      r->integer = env->SetVar(q->text, value());
      break;
    case AVS_CX_SDK_SET_GLOBAL_VAR:
      r->integer = env->SetGlobalVar(q->text, value());
      break;
    case AVS_CX_SDK_FUNCTION_EXISTS:
      r->integer = env->FunctionExists(q->text);
      break;
    case AVS_CX_SDK_ENV_PROPERTY:
      r->integer = env->GetEnvProperty(static_cast<AvsEnvProperty>(q->integer));
      break;
    case AVS_CX_SDK_CHECK_VERSION:
      env->CheckVersion(static_cast<int>(q->integer));
      break;
    case AVS_CX_SDK_ACQUIRE_LOCK:
      r->integer = env->AcquireGlobalLock(q->text);
      break;
    case AVS_CX_SDK_RELEASE_LOCK:
      env->ReleaseGlobalLock(q->text);
      break;
    case AVS_CX_SDK_COPY_PROPS: {
      auto src = frame(q->frame);
      auto dst = frame(q->other_frame);
      env->copyFrameProps(src, dst);
      break;
    }
    case AVS_CX_SDK_PROPS_RO:
      r->pointer = const_cast<AVSMap *>(env->getFramePropsRO(frame(q->frame)));
      break;
    case AVS_CX_SDK_PROPS_RW: {
      auto f = frame(q->frame);
      r->pointer = env->getFramePropsRW(f);
      break;
    }
    case AVS_CX_SDK_SUBFRAME: {
      auto src = frame(q->frame);
      PVideoFrame dst;
      const auto *i = q->integers;
      if (q->integer == 0)
        dst = env->Subframe(src, int(i[0]), int(i[1]), int(i[2]), int(i[3]));
      else if (q->integer == 1)
        dst = env->SubframePlanar(src, int(i[0]), int(i[1]), int(i[2]), int(i[3]), int(i[4]),
                                  int(i[5]), int(i[6]));
      else
        dst = env->SubframePlanarA(src, int(i[0]), int(i[1]), int(i[2]), int(i[3]), int(i[4]),
                                   int(i[5]), int(i[6]), int(i[7]));
      r->frame = {CxCoreFrameAccess::Detach(&dst), &HostFrameOperations()};
      break;
    }
    case AVS_CX_SDK_MAKE_PROPERTY_WRITABLE: {
      auto f = frame(q->frame);
      env->MakePropertyWritable(&f);
      r->frame = {CxCoreFrameAccess::Detach(&f), &HostFrameOperations()};
      break;
    }
    case AVS_CX_SDK_INVOKE2:
      if (!q->other_value)
        throw std::invalid_argument("CX SDK missing implicit last");
      result(env->Invoke2(ValueFromCx(*q->other_value, session, env), q->text, value(), q->names));
      break;
    case AVS_CX_SDK_PROP_GET_CLIP:
      result(AVSValue(env->propGetClip(static_cast<const AVSMap *>(q->pointers[0]), q->text,
                                       int(q->integer),
                                       static_cast<int *>(const_cast<void *>(q->pointers[1])))));
      break;
    case AVS_CX_SDK_PROP_SET_CLIP: {
      PClip clip = value().AsClip();
      r->integer = env->propSetClip(static_cast<AVSMap *>(const_cast<void *>(q->pointers[0])),
                                    q->text, clip, int(q->integer));
      break;
    }
    case AVS_CX_SDK_PROP_GET_FRAME: {
      auto f =
          env->propGetFrame(static_cast<const AVSMap *>(q->pointers[0]), q->text, int(q->integer),
                            static_cast<int *>(const_cast<void *>(q->pointers[1])));
      if (f)
        r->frame = {CxCoreFrameAccess::Detach(&f), &HostFrameOperations()};
      break;
    }
    case AVS_CX_SDK_PROP_SET_FRAME:
      r->integer = env->propSetFrame(static_cast<AVSMap *>(const_cast<void *>(q->pointers[0])),
                                     q->text, frame(q->frame), int(q->integer));
      break;
#include "../../include/avs/cx/sdk/host_dispatch.inc"
    default:
      return AVS_CX_STATUS_UNSUPPORTED;
    }
    return AVS_CX_STATUS_OK;
  } catch (const IScriptEnvironment::NotFound &) {
    SetCxError(error, AVS_CX_STATUS_FEATURE_NOT_FOUND, "CX SDK function or variable not found");
    return AVS_CX_STATUS_FEATURE_NOT_FOUND;
  } catch (const AvisynthError &e) {
    SetCxErrorCopy(error, AVS_CX_STATUS_HOST_ERROR, e.msg);
  } catch (const std::exception &e) {
    SetCxErrorCopy(error, AVS_CX_STATUS_HOST_ERROR, e.what());
  } catch (...) {
    SetCxError(error, AVS_CX_STATUS_HOST_ERROR, "CX SDK host exception");
  }
  return AVS_CX_STATUS_HOST_ERROR;
}
