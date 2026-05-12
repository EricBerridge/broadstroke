#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace broadstroke::analyzer {

// Sobel gradient. Output gx and gy are per-pixel signed gradients
// (∂L/∂x and ∂L/∂y). Edge magnitude is sqrt(gx² + gy²).
struct SobelGradient {
  std::vector<float> gx;
  std::vector<float> gy;
};

SobelGradient sobelGradient(std::span<const float> luminance,
                            std::uint32_t width, std::uint32_t height);

// Convenience: gradient magnitude only.
std::vector<float> sobelEdges(std::span<const float> luminance,
                              std::uint32_t width, std::uint32_t height);

}  // namespace broadstroke::analyzer
