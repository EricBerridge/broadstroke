#pragma once

#include <cstdint>
#include <span>

namespace broadstroke::reference {

struct PastelStrokeParams {
  float tip_radius_px;
  float tip_softness;
  float deposit_strength;
  std::uint32_t offset_x = 0;
  std::uint32_t offset_y = 0;
  std::uint32_t extent_w = 0;
  std::uint32_t extent_h = 0;
};

void pastel_stroke(std::span<float> pigment,
                   std::span<const float> tooth,
                   std::span<const float> stroke,
                   std::uint32_t canvas_width,
                   std::uint32_t canvas_height,
                   const PastelStrokeParams& params);

}  // namespace broadstroke::reference
