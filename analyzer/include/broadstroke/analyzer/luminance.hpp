#pragma once

#include "broadstroke/analyzer/image.hpp"

#include <cstdint>
#include <vector>

namespace broadstroke::analyzer {

// Linear-RGB → relative luminance using Rec. 709 weights. Output is one
// scalar per pixel in [0, 1].
std::vector<float> luminance(const Image& img);

// Quantize luminance into N equal-width bands, returning a per-pixel
// band index in [0, num_bands - 1]. Lowest band = darkest. Bands map
// directly to wash strength in the planner.
std::vector<std::uint8_t> valueBands(const std::vector<float>& lum,
                                     std::uint8_t num_bands);

}  // namespace broadstroke::analyzer
