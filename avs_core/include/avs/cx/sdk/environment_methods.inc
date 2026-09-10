// Explicit failures for API methods outside the current experimental subset.
void *__stdcall ManageCache(int key, void *data) override {
  throw AvisynthError("CX SDK: ManageCache is not implemented");
}
void __stdcall DeleteScriptEnvironment() override {
  throw AvisynthError("CX SDK: DeleteScriptEnvironment is not implemented");
}
void __stdcall ApplyMessage(PVideoFrame *frame, const VideoInfo &vi, const char *message, int size,
                            int textcolor, int halocolor, int bgcolor) override {
  ApplyMessageEx(frame, vi, message, size, textcolor, halocolor, bgcolor, false);
}
void __stdcall ApplyMessageEx(PVideoFrame *frame, const VideoInfo &vi, const char *message, int size,
                              int textcolor, int halocolor, int bgcolor, bool utf8) override {
  if (!frame || !*frame || !message)
    ThrowError("CX SDK: invalid message arguments");
  const void *feature = nullptr;
  auto status = session->host->query_feature(session->host->host_context, AVS_CX_FEATURE_MESSAGE,
      1, sizeof(avs_cx_message_feature_v1), &feature);
  if (status != AVS_CX_STATUS_OK || !feature)
    ThrowError("CX SDK: host does not support message rendering");
  auto *api = static_cast<const avs_cx_message_feature_v1 *>(feature);
  if (api->struct_size < sizeof(*api) || api->abi_version != 1 || !api->apply)
    ThrowError("CX SDK: invalid message rendering feature");
  const size_t length = std::strlen(message);
  if (length > (std::numeric_limits<uint32_t>::max)())
    ThrowError("CX SDK: message too large");
  avs_cx_message_request_v1 q{};
  q.struct_size = sizeof(q);
  q.utf8 = utf8 ? 1 : 0;
  q.source = Frame::From(frame->operator->())->ref.get();
  q.video_info = ToInfo(vi);
  q.message.data = message;
  q.message.size = static_cast<uint32_t>(length);
  q.size = size;
  q.text_color = textcolor;
  q.halo_color = halocolor;
  q.background_color = bgcolor;
  avs_cx_frame_ref_v1 out{};
  avs_cx_error_v1 e{};
  e.struct_size = sizeof(e);
  status = api->apply(api->context, Call().environment, &q, &out, &e);
  auto owned = avs::cx::frame::adopt(out);
  Check(status, e);
  *frame = Wrap(owned.detach(), session->sdk);
}
AVSValue __stdcall Invoke3(const AVSValue &implicit_last, const PFunction &func,
                           const AVSValue args, const char *const *arg_names = 0) override {
  throw AvisynthError("CX SDK: Invoke3 is not implemented");
}
bool __stdcall Invoke3Try(AVSValue *result, const AVSValue &implicit_last, const PFunction &func,
                          const AVSValue args, const char *const *arg_names = 0) override {
  throw AvisynthError("CX SDK: Invoke3Try is not implemented");
}
