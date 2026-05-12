#include "image_writer.hpp"
#include "bmp_writer.hpp"

// stb_image_write is single-header. analyzer/src/load.cpp owns
// STB_IMAGE_IMPLEMENTATION; this TU owns STB_IMAGE_WRITE_IMPLEMENTATION.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace broadstroke::io {

namespace {

std::string lowercaseExt(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext;
}

}  // namespace

bool writeImage(const std::filesystem::path& path,
                std::uint32_t width,
                std::uint32_t height,
                std::span<const std::uint8_t> rgb) {
  if (static_cast<std::size_t>(width) * height * 3 != rgb.size()) {
    return false;
  }

  const std::string ext = lowercaseExt(path);
  const std::string p   = path.string();
  const int w = static_cast<int>(width);
  const int h = static_cast<int>(height);

  if (ext == ".png") {
    // stride_in_bytes = w*3 (row-major, no padding).
    return stbi_write_png(p.c_str(), w, h, 3, rgb.data(), w * 3) != 0;
  }
  if (ext == ".jpg" || ext == ".jpeg") {
    return stbi_write_jpg(p.c_str(), w, h, 3, rgb.data(), /*quality=*/92) != 0;
  }
  // Default / .bmp: use the in-tree writer so BMP output works without stb.
  return writeBmp24(path, width, height, rgb);
}

}  // namespace broadstroke::io
