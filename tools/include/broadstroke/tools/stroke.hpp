#pragma once

#include <cstdint>
#include <vector>

namespace broadstroke::tools {

// A single sample along a stroke path. Position is in canvas pixels;
// pressure is normalized [0, 1] like a tablet stylus reports.
struct StrokePoint {
  float x;
  float y;
  float pressure;
};

// A polyline stroke. The renderer walks each segment p[i]->p[i+1] and
// accumulates deposit along its arc. Two points is the minimum useful
// stroke.
struct Stroke {
  std::vector<StrokePoint> points;
};

// Flatten to the packed [x0, y0, p0, x1, y1, p1, ...] layout the GPU
// kernel binds. Returned size is 3 * points.size() floats.
std::vector<float> packStroke(const Stroke& s);

// Resample a stroke at uniform arclength spacing of approximately
// `spacing` pixels. The original endpoints are preserved; pressure is
// linearly interpolated. Returned stroke has at least 2 points.
//
// Resampling is not strictly necessary for the per-pixel kernel — it
// already handles long segments — but it produces smoother demo curves
// when the source path is sparse.
Stroke resampleUniform(const Stroke& source, float spacing);

}  // namespace broadstroke::tools
