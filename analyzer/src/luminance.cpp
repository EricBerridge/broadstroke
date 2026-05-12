#include "broadstroke/analyzer/luminance.hpp"

#include <algorithm>

namespace broadstroke::analyzer {

std::vector<float> luminance(const Image& img) {
  const std::size_t n = img.pixelCount();
  std::vector<float> y(n);
  for (std::size_t i = 0; i < n; ++i) {
    const float r = img.rgba[i * 4 + 0];
    const float g = img.rgba[i * 4 + 1];
    const float b = img.rgba[i * 4 + 2];
    // Rec. 709 weights — same coefficients used for sRGB→Y'.
    y[i] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
  }
  return y;
}

std::vector<std::uint8_t> valueBands(const std::vector<float>& lum,
                                     std::uint8_t num_bands) {
  if (num_bands == 0) num_bands = 1;
  std::vector<std::uint8_t> out(lum.size());
  const float fb = static_cast<float>(num_bands);
  for (std::size_t i = 0; i < lum.size(); ++i) {
    int band = static_cast<int>(lum[i] * fb);
    band = std::clamp(band, 0, static_cast<int>(num_bands) - 1);
    out[i] = static_cast<std::uint8_t>(band);
  }
  return out;
}

}  // namespace broadstroke::analyzer
