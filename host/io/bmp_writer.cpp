#include "bmp_writer.hpp"

#include <cstring>
#include <fstream>
#include <vector>

namespace broadstroke::io {

namespace {

inline void write_u16(std::ofstream& f, std::uint16_t v) {
  f.put(static_cast<char>(v & 0xFF));
  f.put(static_cast<char>((v >> 8) & 0xFF));
}

inline void write_u32(std::ofstream& f, std::uint32_t v) {
  f.put(static_cast<char>(v & 0xFF));
  f.put(static_cast<char>((v >> 8) & 0xFF));
  f.put(static_cast<char>((v >> 16) & 0xFF));
  f.put(static_cast<char>((v >> 24) & 0xFF));
}

}  // namespace

bool writeBmp24(const std::filesystem::path& path,
                std::uint32_t width,
                std::uint32_t height,
                std::span<const std::uint8_t> rgb) {
  if (static_cast<std::size_t>(width) * height * 3 != rgb.size()) {
    return false;
  }

  // Rows in BMP are padded to a 4-byte boundary.
  const std::uint32_t row_size_padded = ((width * 3u + 3u) / 4u) * 4u;
  const std::uint32_t image_size      = row_size_padded * height;
  const std::uint32_t file_size       = 14u + 40u + image_size;

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;

  // BITMAPFILEHEADER (14 bytes)
  write_u16(f, 0x4D42);    // "BM"
  write_u32(f, file_size);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u32(f, 14u + 40u);  // pixel data offset

  // BITMAPINFOHEADER (40 bytes)
  write_u32(f, 40);
  write_u32(f, width);
  write_u32(f, height);     // positive: rows stored bottom-up
  write_u16(f, 1);
  write_u16(f, 24);
  write_u32(f, 0);          // BI_RGB
  write_u32(f, image_size);
  write_u32(f, 2835);       // 72 DPI in pixels/meter
  write_u32(f, 2835);
  write_u32(f, 0);
  write_u32(f, 0);

  // Pixel data: input is top-down RGB; BMP wants bottom-up BGR.
  std::vector<std::uint8_t> row(row_size_padded, 0);
  for (std::int64_t y = static_cast<std::int64_t>(height) - 1; y >= 0; --y) {
    const std::uint8_t* src = rgb.data() + static_cast<std::size_t>(y) * width * 3u;
    for (std::uint32_t x = 0; x < width; ++x) {
      row[x * 3u + 0u] = src[x * 3u + 2u];  // B
      row[x * 3u + 1u] = src[x * 3u + 1u];  // G
      row[x * 3u + 2u] = src[x * 3u + 0u];  // R
    }
    f.write(reinterpret_cast<const char*>(row.data()),
            static_cast<std::streamsize>(row_size_padded));
  }
  return f.good();
}

}  // namespace broadstroke::io
