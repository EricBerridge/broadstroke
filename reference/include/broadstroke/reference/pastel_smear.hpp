#pragma once

#include <cstdint>
#include <span>

namespace broadstroke::reference {

struct PastelSmearParams {
  float ax, ay, bx, by;        // drag segment endpoints
  float smear_radius_px;
  float smear_softness;
  std::uint32_t smear_taps;
  float smear_strength;
  std::uint32_t offset_x = 0;
  std::uint32_t offset_y = 0;
  std::uint32_t extent_w = 0;
  std::uint32_t extent_h = 0;
};

void pastel_smear(std::span<float> pigment_out,
                  std::span<const float> pigment_in,
                  std::uint32_t canvas_width,
                  std::uint32_t canvas_height,
                  const PastelSmearParams& params);

}  // namespace broadstroke::reference
