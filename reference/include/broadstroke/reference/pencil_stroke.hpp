#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace broadstroke::reference {

// CPU mirror of compute/kernels/pencil_stroke.comp.
struct PencilStrokeParams {
  float tip_radius_px;
  float tip_softness;
  float deposit_strength;
  // Bounded-box dispatch (Stage 4). 0 extents mean "rest of canvas".
  std::uint32_t offset_x = 0;
  std::uint32_t offset_y = 0;
  std::uint32_t extent_w = 0;
  std::uint32_t extent_h = 0;
};

void pencil_stroke(std::span<float> graphite,
                   std::span<const float> tooth,
                   std::span<const float> stroke,
                   std::uint32_t canvas_width,
                   std::uint32_t canvas_height,
                   const PencilStrokeParams& params);

}  // namespace broadstroke::reference
