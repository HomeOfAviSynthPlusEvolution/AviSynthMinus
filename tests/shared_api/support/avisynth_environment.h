#pragma once

#include <avisynth.h>

#include <stdexcept>

extern const AVS_Linkage* AVS_linkage;

namespace avsut::test {

class AviSynthEnvironment {
 public:
  AviSynthEnvironment() : environment_(CreateScriptEnvironment2()) {
    if (environment_ == nullptr) {
      throw std::runtime_error("CreateScriptEnvironment2 failed");
    }
    AVS_linkage = environment_->GetAVSLinkage();
  }

  ~AviSynthEnvironment() {
    if (environment_ != nullptr) {
      environment_->DeleteScriptEnvironment();
    }
  }

  AviSynthEnvironment(const AviSynthEnvironment&) = delete;
  AviSynthEnvironment& operator=(const AviSynthEnvironment&) = delete;

  IScriptEnvironment* get() const noexcept { return environment_; }

 private:
  IScriptEnvironment2* environment_{};
};

}  // namespace avsut::test
