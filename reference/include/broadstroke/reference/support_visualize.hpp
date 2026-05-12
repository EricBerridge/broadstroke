#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace broadstroke::reference {

// CPU mirror of compute/kernels/support_visualize.comp. Must produce
// bit-equivalent output up to floating-point rounding so the physics
// test can assert agreement.
struct VisualizeParams {
  std::array<float, 3> base_color;
  std::array<float, 3> light_dir;  // pre-normalized
  float ambient;
  float height_scale;
};

// `heightfield` is width*height floats. Returns width*height*4 floats
// (RGBA) in row-major, pixel-major layout matching the GPU kernel.
std::vector<float> support_visualize(std::span<const float> heightfield,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     const VisualizeParams& params);

}  // namespace broadstroke::reference
