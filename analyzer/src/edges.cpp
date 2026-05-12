#include "broadstroke/analyzer/edges.hpp"

#include <cmath>

namespace broadstroke::analyzer {

namespace {

inline float at(std::span<const float> src, std::uint32_t w, std::uint32_t h,
                int x, int y) {
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (static_cast<std::uint32_t>(x) >= w) x = static_cast<int>(w) - 1;
  if (static_cast<std::uint32_t>(y) >= h) y = static_cast<int>(h) - 1;
  return src[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)];
}

}  // namespace

SobelGradient sobelGradient(std::span<const float> luminance,
                            std::uint32_t width, std::uint32_t height) {
  SobelGradient g;
  g.gx.resize(static_cast<std::size_t>(width) * height);
  g.gy.resize(static_cast<std::size_t>(width) * height);

  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const float a = at(luminance, width, height, x - 1, y - 1);
      const float b = at(luminance, width, height, x    , y - 1);
      const float c = at(luminance, width, height, x + 1, y - 1);
      const float d = at(luminance, width, height, x - 1, y    );
      const float f = at(luminance, width, height, x + 1, y    );
      const float p = at(luminance, width, height, x - 1, y + 1);
      const float q = at(luminance, width, height, x    , y + 1);
      const float r = at(luminance, width, height, x + 1, y + 1);

      // Standard 3×3 Sobel kernels:
      //   Gx = [-1 0 1; -2 0 2; -1 0 1] / 8
      //   Gy = [-1 -2 -1; 0 0 0; 1 2 1] / 8
      // Divide-by-8 normalises so a unit step gives gradient magnitude 1.
      const float gx = ((c - a) + 2.0f * (f - d) + (r - p)) * 0.125f;
      const float gy = ((p - a) + 2.0f * (q - b) + (r - c)) * 0.125f;

      const std::size_t i = static_cast<std::size_t>(y) * width + x;
      g.gx[i] = gx;
      g.gy[i] = gy;
    }
  }
  return g;
}

std::vector<float> sobelEdges(std::span<const float> luminance,
                              std::uint32_t width, std::uint32_t height) {
  auto g = sobelGradient(luminance, width, height);
  std::vector<float> mag(g.gx.size());
  for (std::size_t i = 0; i < mag.size(); ++i) {
    mag[i] = std::sqrt(g.gx[i] * g.gx[i] + g.gy[i] * g.gy[i]);
  }
  return mag;
}

}  // namespace broadstroke::analyzer
