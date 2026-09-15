#include "internal_filters_core/registration.h"
#include "core/function.h"
#include "core/internal.h"
#include <array>
#include <utility>
#include "blank_clip/filters.h"
#include "channel_display/filters.h"
#include "color_adjust/filters.h"
#include "color_bars/filters.h"
#include "convolution/filters.h"
#include "crop/filters.h"
#include "focus/filters.h"
#include "greyscale/filters.h"
#include "invert/filters.h"
#include "limiter/filters.h"
#include "merge/filters.h"
#include "planes/filters.h"
#include "rgb_merge/filters.h"
#include "rotation/filters.h"
#include "rows_columns/filters.h"
#include "stack/filters.h"

namespace avs::internal_filters {
namespace {
template <size_t N>
class BuiltinTable {
  AVSFunction entries_[N + 1];
  template <size_t... I>
  BuiltinTable(const std::array<aif::filters::Registration, N>& r, std::index_sequence<I...>)
      : entries_{{r[I].name, BUILTIN_FUNC_PREFIX, r[I].arguments, r[I].create, r[I].user_data}..., {nullptr}} {}

public:
  explicit BuiltinTable(const std::array<aif::filters::Registration, N>& r)
      : BuiltinTable(r, std::make_index_sequence<N>{}) {}
  const AVSFunction* data() const { return entries_; }
};
} // namespace
const AVSFunction* blank_clip() {
  static const BuiltinTable table(aif::filters::blank_clip::registrations());
  return table.data();
}
const AVSFunction* channel_display() {
  static const BuiltinTable table(aif::filters::channel_display::registrations());
  return table.data();
}
const AVSFunction* color_adjust() {
  static const BuiltinTable table(aif::filters::color_adjust::registrations());
  return table.data();
}
const AVSFunction* color_bars() {
  static const BuiltinTable table(aif::filters::color_bars::registrations());
  return table.data();
}
const AVSFunction* convolution() {
  static const BuiltinTable table(aif::filters::convolution::registrations());
  return table.data();
}
const AVSFunction* crop() {
  static const BuiltinTable table(aif::filters::crop::registrations());
  return table.data();
}
const AVSFunction* focus() {
  static const BuiltinTable table(aif::filters::focus::registrations());
  return table.data();
}
const AVSFunction* greyscale() {
  static const BuiltinTable table(aif::filters::greyscale::registrations());
  return table.data();
}
const AVSFunction* invert() {
  static const BuiltinTable table(aif::filters::invert::registrations());
  return table.data();
}
const AVSFunction* limiter() {
  static const BuiltinTable table(aif::filters::limiter::registrations());
  return table.data();
}
const AVSFunction* merge() {
  static const BuiltinTable table(aif::filters::merge::registrations());
  return table.data();
}
const AVSFunction* planes() {
  static const BuiltinTable table(aif::filters::planes::registrations());
  return table.data();
}
const AVSFunction* rgb_merge() {
  static const BuiltinTable table(aif::filters::rgb_merge::registrations());
  return table.data();
}
const AVSFunction* rotation() {
  static const BuiltinTable table(aif::filters::rotation::registrations());
  return table.data();
}
const AVSFunction* rows_columns() {
  static const BuiltinTable table(aif::filters::rows_columns::registrations());
  return table.data();
}
const AVSFunction* stack() {
  static const BuiltinTable table(aif::filters::stack::registrations());
  return table.data();
}
} // namespace avs::internal_filters
