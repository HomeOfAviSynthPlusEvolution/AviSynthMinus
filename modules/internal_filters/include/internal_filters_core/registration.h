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
} // namespace avs::internal_filters
