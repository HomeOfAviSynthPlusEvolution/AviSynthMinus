#pragma once

#include <avisynth.h>

#include <stdexcept>

namespace avsut::test {

class AviSynthEnvironment {
 public:
  AviSynthEnvironment() : environment_(CreateScriptEnvironment2()) {
    if (environment_ == nullptr) {
      throw std::runtime_error("CreateScriptEnvironment2 failed");
    }
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
