#pragma once
class AVSFunction;
namespace avs::internal_filters {
const AVSFunction* blank_clip();
const AVSFunction* channel_display();
const AVSFunction* color_adjust();
const AVSFunction* color_bars();
const AVSFunction* convolution();
const AVSFunction* crop();
const AVSFunction* focus();
const AVSFunction* greyscale();
const AVSFunction* invert();
const AVSFunction* limiter();
const AVSFunction* merge();
const AVSFunction* planes();
const AVSFunction* rgb_merge();
const AVSFunction* rotation();
const AVSFunction* rows_columns();
const AVSFunction* stack();
const AVSFunction *mask();
const AVSFunction *layer();
const AVSFunction *multi_overlay();
const AVSFunction *overlay();
const AVSFunction *histogram();
const AVSFunction *frame_rate();
const AVSFunction *field();
const AVSFunction *frame_select();
const AVSFunction *legacy_correction();
} // namespace avs::internal_filters
