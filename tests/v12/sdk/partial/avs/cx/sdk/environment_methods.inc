// Explicit failures for API methods outside the current experimental subset.
void *__stdcall ManageCache(int key, void *data) override {
  throw AvisynthError("CX SDK: ManageCache is not implemented");
}
void __stdcall DeleteScriptEnvironment() override {
  throw AvisynthError("CX SDK: DeleteScriptEnvironment is not implemented");
}
void __stdcall ApplyMessage(PVideoFrame *frame, const VideoInfo &vi, const char *message, int size,
                            int textcolor, int halocolor, int bgcolor) override {
  throw AvisynthError("CX SDK: ApplyMessage is not implemented");
}
AVSValue __stdcall Invoke3(const AVSValue &implicit_last, const PFunction &func,
                           const AVSValue args, const char *const *arg_names = 0) override {
  throw AvisynthError("CX SDK: Invoke3 is not implemented");
}
bool __stdcall Invoke3Try(AVSValue *result, const AVSValue &implicit_last, const PFunction &func,
                          const AVSValue args, const char *const *arg_names = 0) override {
  throw AvisynthError("CX SDK: Invoke3Try is not implemented");
}
