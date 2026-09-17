#include <array>
#include <avs_iris/expr.h>
#include <cstring>
#include <iris/host.h>
#include <iris/iris.h>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct PlanDelete {
  void operator()(iris_plan *p) const { iris_plan_destroy(p); }
};
struct ContextDelete {
  void operator()(iris_context *p) const { iris_context_destroy(p); }
};
struct LutDelete {
  void operator()(iris_host_lut *p) const { iris_host_lut_destroy(p); }
};
struct Plane {
  std::unique_ptr<iris_plan, PlanDelete> plan;
  std::unique_ptr<iris_host_lut, LutDelete> lut;
  uint64_t lut_bytes = 0;
  iris_plan_info info{};
  std::vector<iris_property_dependency> properties;
  int id = 0;
  bool copy = false;
};
class Filter final : public GenericVideoFilter {
public:
  std::vector<PClip> inputs;
  std::vector<VideoInfo> formats;
  std::array<Plane, 4> planes;
  int count = 0;
  uint32_t used = 0;
  explicit Filter(PClip first) : GenericVideoFilter(first) {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override;
  int __stdcall SetCacheHints(int hint, int) override {
    return hint == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
  }
  void set_format(const VideoInfo &info) { vi = info; }
};
void check(iris_status status, const iris_diagnostic &d) {
  if (status != IRIS_OK)
    throw std::runtime_error(std::string("IrisExpr: ") + d.message);
}
int plane_id(const VideoInfo &vi, int index) {
  constexpr int yuv[] = {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
  constexpr int rgb[] = {PLANAR_R, PLANAR_G, PLANAR_B, PLANAR_A};
  return vi.IsRGB() ? rgb[index] : yuv[index];
}
iris_format format(const VideoInfo &vi) {
  auto bits = uint32_t(vi.BitsPerComponent());
  return {bits == 8 ? IRIS_U8 : bits == 32 ? IRIS_F32 : IRIS_U16, bits};
}
uint32_t width(const VideoInfo &vi, int plane) {
  return uint32_t(vi.width >> vi.GetPlaneWidthSubsampling(plane));
}
uint32_t height(const VideoInfo &vi, int plane) {
  return uint32_t(vi.height >> vi.GetPlaneHeightSubsampling(plane));
}
int input_plane(const Filter &f, size_t input, int output_index) {
  const auto &vi = f.formats[input];
  auto components = vi.NumComponents();
  if (components == 1)
    return PLANAR_Y;
  if (output_index >= components)
    throw std::runtime_error(
        "IrisExpr: expression references a missing input plane");
  return plane_id(vi, output_index);
}
std::vector<float>
read_properties(IScriptEnvironment *env,
                const std::vector<iris_property_dependency> &dependencies,
                const std::vector<PVideoFrame> &sources) {
  std::vector<float> properties(dependencies.size());
  for (size_t slot = 0; slot < properties.size(); ++slot) {
    const auto &dep = dependencies[slot];
    const auto *map = env->getFramePropsRO(sources[dep.input]);
    char type = env->propGetType(map, dep.name);
    int error = 0;
    if (type == 'f')
      properties[slot] = env->propGetFloatSaturated(map, dep.name, 0, &error);
    else if (type == 'i')
      properties[slot] = float(env->propGetInt(map, dep.name, 0, &error));
    if (error)
      properties[slot] = 0;
  }
  return properties;
}
PVideoFrame __stdcall Filter::GetFrame(int n, IScriptEnvironment *env) {
  const auto &f = *this;
  try {
    std::vector<PVideoFrame> sources(f.inputs.size());
    PVideoFrame property_source;
    for (size_t i = 0; i < f.inputs.size(); ++i)
      if (f.used & (1u << i)) {
        sources[i] = f.inputs[i]->GetFrame(n, env);
        if (!property_source)
          property_source = sources[i];
      }
    auto output = property_source ? env->NewVideoFrameP(vi, &property_source)
                                  : env->NewVideoFrame(vi);
    for (int i = 0; i < f.count; ++i) {
      const auto &plane = f.planes[i];
      auto *dst = output->GetWritePtr(plane.id);
      auto pitch = output->GetPitch(plane.id);
      if (plane.copy) {
        int source_plane = input_plane(f, 0, i);
        auto *src = sources[0]->GetReadPtr(source_plane);
        int src_pitch = sources[0]->GetPitch(source_plane);
        for (int y = 0; y < output->GetHeight(plane.id); ++y)
          std::memcpy(dst + ptrdiff_t(y) * pitch,
                      src + ptrdiff_t(y) * src_pitch,
                      size_t(output->GetRowSize(plane.id)));
        continue;
      }
      if (plane.lut) {
        std::array<iris_input_plane, 2> inputs{};
        for (size_t j = 0; j < f.inputs.size(); ++j)
          if (plane.info.input_mask & (1u << j)) {
            int id = input_plane(f, j, i);
            inputs[j] = {sources[j]->GetReadPtr(id), sources[j]->GetPitch(id)};
          }
        iris_diagnostic d{};
        check(iris_host_lut_apply(plane.lut.get(), inputs.data(), {dst, pitch},
                                  &d),
              d);
        continue;
      }
      auto properties = read_properties(env, plane.properties, sources);
      iris_diagnostic d{};
      iris_context *raw = nullptr;
      check(iris_context_create(plane.plan.get(), &raw, &d), d);
      std::unique_ptr<iris_context, ContextDelete> context(raw);
      iris_execute_args_v1 a{};
      a.struct_size = sizeof(a);
      a.input_count = uint32_t(f.inputs.size());
      a.frameno = uint64_t(n);
      a.output = {dst, pitch};
      a.properties = properties.data();
      a.property_count = properties.size();
      for (size_t j = 0; j < f.inputs.size(); ++j)
        if (plane.info.input_mask & (1u << j)) {
          auto id = input_plane(f, j, i);
          a.inputs[j] = {sources[j]->GetReadPtr(id), sources[j]->GetPitch(id)};
        }
      check(iris_execute_v1(plane.plan.get(), context.get(), &a, &d), d);
    }
    return output;
  } catch (const std::exception &e) {
    env->ThrowError("%s", e.what());
  }
  return {};
}
std::string string_option(AVSValue args, int index, const char *fallback) {
  return args[index].AsString(fallback);
}
bool bool_option(AVSValue args, int index, bool fallback) {
  return args[index].AsBool(fallback);
}
int int_option(AVSValue args, int index, int fallback) {
  return args[index].AsInt(fallback);
}
} // namespace
AVSValue __cdecl CreateExprCompat(AVSValue args, void *,
                                  IScriptEnvironment *env) {
  // Preserve the historical Expr argument positions. Its four execution
  // tuning flags are accepted by the signature but have no effect on Iris.
  AVSValue iris_args[] = {args[0], args[1], args[2],  args[11], args[6],
                          args[7], args[8], args[12], args[9],  args[13]};
  return CreateIrisExpr(AVSValue(iris_args, 10), nullptr, env);
}

AVSValue __cdecl CreateIrisExpr(AVSValue args, void *,
                                IScriptEnvironment *env) {
  try {
    iris_diagnostic diag{};
    const auto &clips = args[0];
    const auto &expressions = args[1];
    int inputs = clips.ArraySize(), expr_count = expressions.ArraySize();
    if (inputs < 1 || inputs > 26)
      throw std::runtime_error("IrisExpr: expected 1..26 clips");
    auto f = std::make_unique<Filter>(clips[0].AsClip());
    int lut_mode = int_option(args, 8, 0);
    int lut_max_mb = int_option(args, 9, 256);
    check(iris_host_lut_check_budget(0, lut_max_mb, &diag), diag);
    if (lut_mode < 0 || lut_mode > 2)
      throw std::runtime_error("IrisExpr: lut must be 0, 1 or 2");
    if (lut_mode && inputs != lut_mode)
      throw std::runtime_error(
          "IrisExpr: input clip count must equal the LUT dimension");
    for (int i = 0; i < inputs; ++i) {
      auto clip = clips[i].AsClip();
      auto vi = clip->GetVideoInfo();
      if (!vi.IsPlanar())
        throw std::runtime_error("IrisExpr: planar input required");
      if (lut_mode && format(vi).type == IRIS_F32)
        throw std::runtime_error("IrisExpr: manual LUT requires integer "
                                 "inputs; use lut=0 for floating-point inputs");
      if (!f->formats.empty()) {
        const auto &first = f->formats[0];
        if (vi.width != first.width || vi.height != first.height ||
            vi.NumComponents() != first.NumComponents())
          throw std::runtime_error(
              "IrisExpr: input dimensions and plane counts must match");
        for (int p = 0; p < vi.NumComponents(); ++p)
          if (width(vi, plane_id(vi, p)) != width(first, plane_id(first, p)) ||
              height(vi, plane_id(vi, p)) != height(first, plane_id(first, p)))
            throw std::runtime_error("IrisExpr: input subsampling must match");
      }
      f->formats.push_back(vi);
      f->inputs.push_back(std::move(clip));
    }
    auto vi = f->formats[0];
    auto output_format = string_option(args, 2, "");
    if (!output_format.empty()) {
      AVSValue values[] = {clips[0], output_format.c_str()};
      const char *names[] = {nullptr, "pixel_type"};
      vi.pixel_type = env->Invoke("BlankClip", AVSValue(values, 2), names)
                          .AsClip()
                          ->GetVideoInfo()
                          .pixel_type;
    }
    if (!vi.IsPlanar())
      throw std::runtime_error("IrisExpr: planar output required");
    f->count = vi.NumComponents();
    if (f->count < 1 || f->count > 4 || expr_count < 1 || expr_count > f->count)
      throw std::runtime_error("IrisExpr: invalid expression count");
    auto backend_name = string_option(args, 3, AVS_IRIS_DEFAULT_BACKEND);
    if (backend_name != "scalar" && backend_name != "llvm" &&
        backend_name != "sleef" && backend_name != "sleef-fast")
      throw std::runtime_error("IrisExpr: unknown backend");
    auto scaling = string_option(args, 4, "none");
    for (auto &c : scaling)
      if (c >= 'A' && c <= 'Z')
        c = char(c - 'A' + 'a');
    const char *modes[] = {"none", "all",   "allf",   "int",
                           "intf", "float", "floatf", "floatuv"};
    int mode = 0;
    while (mode < 8 && scaling != modes[mode])
      ++mode;
    if (mode == 8)
      throw std::runtime_error("IrisExpr: unknown scale_inputs mode");
    for (int p = 0; p < f->count; ++p) {
      auto &plane = f->planes[p];
      plane.id = plane_id(vi, p);
      std::string expression = p < expr_count ? expressions[p].AsString()
                               : p == 3
                                   ? ""
                                   : expressions[expr_count - 1].AsString();
      if (expression.empty() && format(vi).bits != format(f->formats[0]).bits)
        throw std::runtime_error(
            "IrisExpr: plane " + std::to_string(p) +
            " needs an explicit expression when bit depth changes");
      plane.copy = expression.empty();
      iris_compile_options_v1 o{};
      o.struct_size = sizeof(o);
      o.width = width(vi, plane.id);
      o.height = height(vi, plane.id);
      o.input_count = uint32_t(inputs);
      o.output = format(vi);
      o.optimize = bool_option(args, 7, true);
      o.backend = backend_name == "llvm"         ? IRIS_BACKEND_LLVM
                  : backend_name == "sleef"      ? IRIS_BACKEND_SLEEF
                  : backend_name == "sleef-fast" ? IRIS_BACKEND_SLEEF_FAST
                                                 : IRIS_BACKEND_SCALAR;
      for (int j = 0; j < inputs; ++j)
        o.inputs[j] = format(f->formats[j]);
      iris_expr_options_v1 e{};
      e.struct_size = sizeof(e);
      e.frame_count = uint64_t(vi.num_frames);
      e.chroma = !vi.IsRGB() && (p == 1 || p == 2);
      e.scale_inputs = static_cast<iris_scale_inputs>(mode);
      e.clamp_float = bool_option(args, 5, false);
      e.clamp_float_uv = bool_option(args, 6, false);
      if (plane.copy) {
        f->used |= 1;
        // Validate explicit LLVM requests even when all output planes are
        // copied.
        iris_plan *probe = nullptr;
        iris_diagnostic d{};
        auto validation = o;
        validation.optimize = 1;
        check(iris_compile_v1("0", &validation, &probe, &d), d);
        std::unique_ptr<iris_plan, PlanDelete> validated(probe);
        plane.info.input_mask = 1;
      } else {
        if (lut_mode)
          check(iris_host_lut_validate_source(expression.c_str(), &diag), diag);
        iris_plan *raw = nullptr;
        iris_diagnostic d{};
        auto status =
            iris_compile_expr_v1(expression.c_str(), &o, &e, &raw, &d);
        if (status != IRIS_OK)
          throw std::runtime_error("IrisExpr: plane " + std::to_string(p) +
                                   ": " + d.message);
        plane.plan.reset(raw);
        check(iris_plan_get_info(raw, &plane.info, &d), d);
        plane.properties.resize(plane.info.property_count);
        for (size_t slot = 0; slot < plane.properties.size(); ++slot) {
          check(iris_plan_get_property(raw, slot, &plane.properties[slot], &d),
                d);
          f->used |= 1u << plane.properties[slot].input;
        }
        f->used |= plane.info.input_mask;
        if (lut_mode) {
          iris_host_lut *lut = nullptr;
          check(iris_host_lut_create(raw, &lut, &plane.lut_bytes, &diag), diag);
          plane.lut.reset(lut);
        }
      }
      for (int j = 0; j < inputs; ++j) {
        // Geometry is a compile-time contract, even for currently unused
        // inputs.
        int components = f->formats[j].NumComponents();
        if (p >= components && components != 1 &&
            !(plane.info.input_mask & (1u << j)))
          continue;
        int source_plane = input_plane(*f, size_t(j), p);
        if (width(f->formats[j], source_plane) != o.width ||
            height(f->formats[j], source_plane) != o.height)
          throw std::runtime_error(
              "IrisExpr: input and output plane geometry must match");
      }
    }
    if (lut_mode) {
      uint64_t total = 0;
      uint32_t property_inputs = 0;
      for (const auto &plane : f->planes) {
        if (!plane.lut)
          continue;
        if (plane.lut_bytes > std::numeric_limits<uint64_t>::max() - total)
          throw std::runtime_error("IrisExpr: aggregate LUT size overflow");
        total += plane.lut_bytes;
        for (const auto &dep : plane.properties)
          property_inputs |= 1u << dep.input;
      }
      // Check the complete filter before allocating any table or requesting
      // frame 0.
      check(iris_host_lut_check_budget(total, lut_max_mb, &diag), diag);
      std::vector<PVideoFrame> snapshots(f->inputs.size());
      for (size_t j = 0; j < f->inputs.size(); ++j)
        if (property_inputs & (1u << j)) {
          try {
            snapshots[j] = f->inputs[j]->GetFrame(0, env);
          } catch (const AvisynthError &e) {
            env->ThrowError("IrisExpr: LUT frame 0 property snapshot failed "
                            "for input %u: %s",
                            unsigned(j), e.msg);
          }
        }
      for (auto &plane : f->planes)
        if (plane.lut) {
          auto properties = read_properties(env, plane.properties, snapshots);
          check(iris_host_lut_build(plane.lut.get(), properties.data(),
                                    properties.size(), &diag),
                diag);
        }
    }
    f->set_format(vi);
    return PClip(f.release());
  } catch (const std::exception &e) {
    env->ThrowError("%s", e.what());
  }
  return {};
}
