#include <avisynth.h>

#include "support/avisynth_environment.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

const char *DualPluginPath() {
  const char *override_path = std::getenv("AVS_CX_SMOKE_DUAL_PATH");
  return override_path == nullptr || *override_path == '\0' ? CX_SMOKE_DUAL_PATH : override_path;
}

PClip CreateY8Clip(IScriptEnvironment *environment, int frames) {
  const AVSValue arguments[] = {1920, 1080, "Y8", frames + 64};
  const char *names[] = {"width", "height", "pixel_type", "length"};
  return environment->Invoke("BlankClip", AVSValue(arguments, 4), names).AsClip();
}

double Run(const char *plugin_path, const char *filter_name, int frames, int block_size,
           std::uint64_t *checksum) {
  avsut::test::AviSynthEnvironment environment;
  const AVSValue plugin_argument(plugin_path);
  environment.get()->Invoke("LoadPlugin", AVSValue(&plugin_argument, 1));

  const PClip source = CreateY8Clip(environment.get(), frames);
  PClip filtered;
  if (block_size > 0) {
    const AVSValue arguments[] = {source, block_size};
    filtered = environment.get()->Invoke(filter_name, AVSValue(arguments, 2)).AsClip();
  } else {
    const AVSValue argument(source);
    filtered = environment.get()->Invoke(filter_name, AVSValue(&argument, 1)).AsClip();
  }

  for (int i = 0; i < 32; ++i) {
    filtered->GetFrame(i, environment.get());
  }

  std::uint64_t local_checksum = 0;
  const auto begin = Clock::now();
  for (int i = 0; i < frames; ++i) {
    const PVideoFrame frame = filtered->GetFrame(i, environment.get());
    local_checksum += frame->GetReadPtr(PLANAR_Y)[0];
  }
  const auto end = Clock::now();
  *checksum += local_checksum;
  return std::chrono::duration<double, std::micro>(end - begin).count() / frames;
}

struct Summary {
  double median;
  double minimum;
  double maximum;
};

struct PairSummary {
  Summary legacy;
  Summary cx;
};

Summary Summarize(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  return {values[values.size() / 2], values.front(), values.back()};
}

PairSummary MeasurePair(const char *cx_path, const char *filter, int frames, int block_size,
                        int samples, std::uint64_t *checksum) {
  std::vector<double> legacy_values;
  std::vector<double> cx_values;
  legacy_values.reserve(static_cast<size_t>(samples));
  cx_values.reserve(static_cast<size_t>(samples));
  for (int sample = 0; sample < samples; ++sample) {
    // Alternate which ABI runs first so frequency and thermal drift do not
    // systematically favor one side of the comparison.
    if ((sample & 1) == 0) {
      legacy_values.push_back(Run(CX_SMOKE_LEGACY_PATH, filter, frames, block_size, checksum));
      cx_values.push_back(Run(cx_path, filter, frames, block_size, checksum));
    } else {
      cx_values.push_back(Run(cx_path, filter, frames, block_size, checksum));
      legacy_values.push_back(Run(CX_SMOKE_LEGACY_PATH, filter, frames, block_size, checksum));
    }
  }
  return {Summarize(std::move(legacy_values)), Summarize(std::move(cx_values))};
}

void PrintResult(const char *transport, const char *filter, const Summary &result) {
  std::cout << std::left << std::setw(9) << transport << "  " << std::setw(22) << filter
            << std::right << std::fixed << std::setprecision(3) << std::setw(10) << result.median
            << " us/frame  " << std::setw(12) << (1000000.0 / result.median) << " fps  ["
            << result.minimum << ", " << result.maximum << "]\n";
}

} // namespace

int main(int argc, char **argv) {
  int frames = 1000;
  int samples = 5;
  if (argc > 1) {
    frames = std::atoi(argv[1]);
    if (frames <= 0) {
      std::cerr << "frame count must be greater than zero\n";
      return 2;
    }
  }
  if (argc > 2) {
    samples = std::atoi(argv[2]);
    if (samples <= 0 || (samples & 1) == 0) {
      std::cerr << "sample count must be a positive odd number\n";
      return 2;
    }
  }

  const int pass_frames = static_cast<int>(
      std::min<int64_t>(std::numeric_limits<int>::max() - 64,
                        std::max<int64_t>(200000, static_cast<int64_t>(frames) * 200)));

  std::uint64_t checksum = 0;
  std::cout << "1920x1080 Y8, one thread, median of " << samples
            << " samples; brackets are min/max\n";
  std::cout << "pass-through: " << pass_frames << " frames/sample; checker: " << frames
            << " frames/sample\n";

  const PairSummary pass =
      MeasurePair(DualPluginPath(), "CXSmokePassThrough", pass_frames, 0, samples, &checksum);
  const PairSummary checker =
      MeasurePair(DualPluginPath(), "CXCheckerInvert", frames, 16, samples, &checksum);

  PrintResult("Init3", "CXSmokePassThrough", pass.legacy);
  PrintResult("CX1", "CXSmokePassThrough", pass.cx);
  std::cout << "CX1 pass-through delta: " << std::fixed << std::setprecision(3)
            << (pass.cx.median - pass.legacy.median) << " us/frame\n";
  PrintResult("Init3", "CXCheckerInvert", checker.legacy);
  PrintResult("CX1", "CXCheckerInvert", checker.cx);
  std::cout << "CX1 checker delta: " << std::fixed << std::setprecision(3)
            << (checker.cx.median - checker.legacy.median) << " us/frame\n";
  if (std::getenv("AVS_CX_SMOKE_DUAL_PATH") != nullptr) {
    std::cout << "note: CX1 plugin path was overridden; checker delta also includes "
                 "the alternate compiler's pixel-kernel code generation\n";
  }
  std::cout << "checksum: " << checksum << '\n';
  return 0;
}
