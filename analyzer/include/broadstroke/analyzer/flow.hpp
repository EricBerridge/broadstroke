#pragma once

#include "broadstroke/analyzer/edges.hpp"

#include <cstdint>
#include <vector>

namespace broadstroke::analyzer {

// Per-pixel flow vector aligned with the dominant local edge / texture
// direction (perpendicular to the gradient). The structure tensor
// J = [Gx² GxGy; GxGy Gy²] is smoothed by a small box blur, then we
// take the eigenvector of its smaller eigenvalue — that's the direction
// "along" texture, which is what wash strokes should follow.
//
// Output is a unit vector per pixel (or zero where there's no usable
// signal — e.g., perfectly flat regions where the tensor is degenerate).
struct FlowField {
  std::vector<float> dx;
  std::vector<float> dy;
};

// `smooth_radius` controls how aggressively the structure tensor is
// pre-smoothed before eigendecomposition. 3-5 px on a 1k canvas gives a
// stable field that still follows broad shapes; bigger values flatten
// fine detail and produce more macro flow.
FlowField structureTensorFlow(const SobelGradient& gradient,
                              std::uint32_t width, std::uint32_t height,
                              int smooth_radius = 3);

}  // namespace broadstroke::analyzer
