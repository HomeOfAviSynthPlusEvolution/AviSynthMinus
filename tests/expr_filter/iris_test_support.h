#pragma once
#include <avisynth.h>
#include <ostream>
#include <string>
#include <vector>

struct IrisTestBackend {
  const char *name;
  const char *backend;
};

inline void PrintTo(const IrisTestBackend &backend, std::ostream *stream) {
  *stream << backend.name;
}

inline std::vector<IrisTestBackend> iris_test_backends() {
  return {{"Scalar", "scalar"}
#ifdef AVS_TEST_IRIS_LLVM
          ,
          {"LLVM", "llvm"}
#endif
#ifdef AVS_TEST_IRIS_SLEEF
          ,
          {"Sleef", "sleef"},
          {"SleefFast", "sleef-fast"}
#endif
  };
}

inline PClip make_iris_test_filter(const std::vector<PClip> &inputs,
                                   const std::vector<std::string> &expressions,
                                   const IrisTestBackend &backend, int lut,
                                   const char *format,
                                   IScriptEnvironment *env) {
  std::vector<AVSValue> args;
  std::vector<const char *> names;
  for (const auto &clip : inputs) {
    args.emplace_back(clip);
    names.push_back(nullptr);
  }
  for (const auto &expression : expressions) {
    args.emplace_back(expression.c_str());
    names.push_back(nullptr);
  }
  args.emplace_back(backend.backend);
  names.push_back("backend");
  args.emplace_back(lut);
  names.push_back("lut");
  if (format) {
    args.emplace_back(format);
    names.push_back("format");
  }
  return env
      ->Invoke("IrisExpr", AVSValue(args.data(), int(args.size())),
               names.data())
      .AsClip();
}
