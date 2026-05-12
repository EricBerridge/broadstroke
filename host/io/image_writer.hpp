#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace broadstroke::io {

// Write `rgb` (W*H*3 bytes, row-major top-down, RGB order) to disk.
// Format is selected by `path`'s extension (case-insensitive):
//   .bmp                    → 24-bit uncompressed BMP (no extra deps).
//   .png                    → PNG via stb_image_write.
//   .jpg / .jpeg            → JPEG via stb_image_write (quality=92).
//
// Unknown extensions fall back to BMP. Returns false on I/O failure.
bool writeImage(const std::filesystem::path& path,
                std::uint32_t width,
                std::uint32_t height,
                std::span<const std::uint8_t> rgb);

}  // namespace broadstroke::io
