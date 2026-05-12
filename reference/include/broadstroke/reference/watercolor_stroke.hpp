#pragma once

#include <cstdint>
#include <span>

namespace broadstroke::reference {

// CPU mirror of compute/kernels/watercolor_stroke.comp.
struct WatercolorStrokeParams {
  float tip_radius_px;
  float tip_softness;
  float water_load;
  float pigment_load;
  // Bounded-box dispatch origin within the canvas. (0, 0) means
  // process the full canvas. width/height of the dispatch is determined
  // by how many invocations the caller schedules (or by the (offset+ext)
  // bounds in the CPU loop).
  std::uint32_t offset_x = 0;
  std::uint32_t offset_y = 0;
  std::uint32_t extent_w = 0;  // 0 = use canvas_width - offset_x
  std::uint32_t extent_h = 0;  // 0 = use canvas_height - offset_y
};

// Apply one watercolor stroke to in/out pigment + wetness fields, both
// sized canvas_width * canvas_height. tooth must match. stroke is the
// packed [x, y, p, ...] layout from tools::packStroke.
void watercolor_stroke(std::span<float> pigment,
                       std::span<float> wetness,
                       std::span<const float> tooth,
                       std::span<const float> stroke,
                       std::uint32_t canvas_width,
                       std::uint32_t canvas_height,
                       const WatercolorStrokeParams& params);

}  // namespace broadstroke::reference
