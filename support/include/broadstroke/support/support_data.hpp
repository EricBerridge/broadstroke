#pragma once

#include <cstdint>
#include <vector>

namespace broadstroke::support {

// CPU-side multi-layer field describing the painting substrate. Each
// channel is width*height floats in row-major order. Uploaded to the GPU
// by the host as a set of compute::Buffer instances (one per channel) so
// later kernels can bind only the channels they actually read or write.
struct SupportData {
  std::uint32_t width  = 0;
  std::uint32_t height = 0;

  std::vector<float> heightfield;  // surface relief, ~[-1, 1]
  std::vector<float> absorption;   // how readily liquid is pulled in, [0, 1]
  std::vector<float> wetness;      // current water content, [0, 1]; 0 at rest
  std::vector<float> grain_x;      // anisotropy vector x component
  std::vector<float> grain_y;      // anisotropy vector y component
  std::vector<float> tooth;        // derived from heightfield gradient magnitude

  std::size_t pixelCount() const {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  }
};

}  // namespace broadstroke::support
