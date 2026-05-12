#pragma once

#include <cstdint>
#include <vector>

namespace broadstroke::analyzer {

// Linear-RGBA float image, row-major, pixel-major. Channels are in the
// canonical [0, 1] sRGB-linear range; the loader converts from 8-bit
// sRGB → linear on input. Alpha is 1.0 for fully-opaque source photos
// and is preserved through the analyzer passes (which read RGB only).
struct Image {
  std::uint32_t width  = 0;
  std::uint32_t height = 0;
  std::vector<float> rgba;   // 4 * width * height floats

  std::size_t pixelCount() const {
    return static_cast<std::size_t>(width) * height;
  }
};

}  // namespace broadstroke::analyzer
