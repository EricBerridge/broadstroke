#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace broadstroke::io {

// Write `rgb` (width*height*3 bytes, row-major top-down, RGB order) as a
// 24-bit uncompressed BMP. Returns false on I/O failure.
//
// BMP because the entire format fits in one .cpp and is openable on
// Windows with zero extra tooling. PNG can come later via stb when
// distribution matters.
bool writeBmp24(const std::filesystem::path& path,
                std::uint32_t width,
                std::uint32_t height,
                std::span<const std::uint8_t> rgb);

}  // namespace broadstroke::io
