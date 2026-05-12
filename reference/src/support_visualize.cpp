#include "broadstroke/reference/support_visualize.hpp"

#include <algorithm>
#include <cmath>

namespace broadstroke::reference {

namespace {

inline std::array<float, 3> normalize(std::array<float, 3> v) {
  const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len == 0.0f) return {0.0f, 0.0f, 1.0f};
  return {v[0] / len, v[1] / len, v[2] / len};
}

}  // namespace

std::vector<float> support_visualize(std::span<const float> heightfield,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     const VisualizeParams& params) {
  const std::size_t n = static_cast<std::size_t>(width) * height;
  std::vector<float> rgba(n * 4, 0.0f);

  const auto L = normalize(params.light_dir);

  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const std::uint32_t xm = (x == 0)            ? 0          : x - 1;
      const std::uint32_t xp = (x + 1 >= width)    ? width  - 1 : x + 1;
      const std::uint32_t ym = (y == 0)            ? 0          : y - 1;
      const std::uint32_t yp = (y + 1 >= height)   ? height - 1 : y + 1;

      const float h_left  = heightfield[static_cast<std::size_t>(y)  * width + xm];
      const float h_right = heightfield[static_cast<std::size_t>(y)  * width + xp];
      const float h_down  = heightfield[static_cast<std::size_t>(ym) * width + x ];
      const float h_up    = heightfield[static_cast<std::size_t>(yp) * width + x ];

      const float gx = (h_right - h_left) * 0.5f * params.height_scale;
      const float gy = (h_up    - h_down) * 0.5f * params.height_scale;

      auto N = normalize({-gx, -gy, 1.0f});
      const float diffuse = std::max(
          0.0f, N[0] * L[0] + N[1] * L[1] + N[2] * L[2]);
      const float shading = params.ambient + (1.0f - params.ambient) * diffuse;

      const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
      rgba[idx + 0] = params.base_color[0] * shading;
      rgba[idx + 1] = params.base_color[1] * shading;
      rgba[idx + 2] = params.base_color[2] * shading;
      rgba[idx + 3] = 1.0f;
    }
  }
  return rgba;
}

}  // namespace broadstroke::reference
