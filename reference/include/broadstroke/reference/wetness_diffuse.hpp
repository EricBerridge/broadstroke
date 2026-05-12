#pragma once

#include <cstdint>
#include <span>

namespace broadstroke::reference {

struct WetnessDiffuseParams {
  float water_diffusion;
  float pigment_diffusion;
  float dry_rate;
  float dt;
  std::uint32_t offset_x = 0;
  std::uint32_t offset_y = 0;
  std::uint32_t extent_w = 0;
  std::uint32_t extent_h = 0;
};

// One reaction-diffusion step. All buffers are W*H. Reads from *_in,
// writes to *_out.
void wetness_diffuse(std::span<float> pigment_out,
                     std::span<float> wetness_out,
                     std::span<const float> pigment_in,
                     std::span<const float> wetness_in,
                     std::span<const float> absorption,
                     std::uint32_t canvas_width,
                     std::uint32_t canvas_height,
                     const WetnessDiffuseParams& params);

}  // namespace broadstroke::reference
