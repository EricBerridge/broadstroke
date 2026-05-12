#pragma once

#include "broadstroke/analyzer/image.hpp"

#include <cstdint>
#include <filesystem>

namespace broadstroke::analyzer {

// Decode JPG/PNG/BMP from disk into a linear-RGBA float Image. The input
// is assumed to be sRGB-encoded 8-bit per channel; the function converts
// to linear on load.
//
// Validates the header before allocating decode memory. `max_dimension`
// caps both width and height — anything larger is rejected without
// allocation, matching the carried-forward risk note that source
// photographs are untrusted input.
//
// Throws std::runtime_error on missing file, decode error, or oversize.
Image loadImage(const std::filesystem::path& path,
                std::uint32_t max_dimension = 8192);

// Box-filter downscale so the longest side is <= max_side, preserving
// aspect ratio. If both src dimensions are already <= max_side, returns
// the source unchanged. Used by the demo to fit arbitrary photos onto
// a canvas-sized substrate without blowing up GPU memory.
Image fitWithin(const Image& src, std::uint32_t max_side);

}  // namespace broadstroke::analyzer
