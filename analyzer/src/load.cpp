#include "broadstroke/analyzer/load.hpp"

// stb_image is single-header; the implementation lives in this TU only.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include <cmath>
#include <stdexcept>

namespace broadstroke::analyzer {

namespace {

// sRGB encoded byte → linear float in [0, 1]. The piecewise definition
// is what stb_image expects when we manually convert; using an LUT here
// would be faster on big images but the analyzer is not the hot path.
inline float srgbToLinear(std::uint8_t v) {
  const float s = v / 255.0f;
  return s <= 0.04045f ? (s / 12.92f)
                       : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

}  // namespace

Image loadImage(const std::filesystem::path& path, std::uint32_t max_dimension) {
  // Header pre-check: validate dimensions before stb_image allocates the
  // full decoded buffer. Rejects an oversize photo without ever touching
  // ~max² × 4 bytes of decode memory.
  int hdr_w = 0, hdr_h = 0, hdr_c = 0;
  if (!stbi_info(path.string().c_str(), &hdr_w, &hdr_h, &hdr_c)) {
    throw std::runtime_error("loadImage: cannot read header: " + path.string() +
                             " (" + (stbi_failure_reason() ? stbi_failure_reason() : "unknown") + ")");
  }
  if (hdr_w <= 0 || hdr_h <= 0) {
    throw std::runtime_error("loadImage: non-positive dimensions in " + path.string());
  }
  if (static_cast<std::uint32_t>(hdr_w) > max_dimension ||
      static_cast<std::uint32_t>(hdr_h) > max_dimension) {
    throw std::runtime_error("loadImage: " + path.string() +
                             " exceeds max_dimension cap");
  }

  int w = 0, h = 0, channels_in_file = 0;
  unsigned char* px = stbi_load(path.string().c_str(), &w, &h,
                                &channels_in_file, /*req_comp=*/4);
  if (!px) {
    throw std::runtime_error("loadImage: decode failed for " + path.string() +
                             " (" + (stbi_failure_reason() ? stbi_failure_reason() : "unknown") + ")");
  }

  Image img;
  img.width  = static_cast<std::uint32_t>(w);
  img.height = static_cast<std::uint32_t>(h);
  img.rgba.resize(static_cast<std::size_t>(w) * h * 4);

  for (std::size_t i = 0, n = static_cast<std::size_t>(w) * h; i < n; ++i) {
    img.rgba[i * 4 + 0] = srgbToLinear(px[i * 4 + 0]);
    img.rgba[i * 4 + 1] = srgbToLinear(px[i * 4 + 1]);
    img.rgba[i * 4 + 2] = srgbToLinear(px[i * 4 + 2]);
    img.rgba[i * 4 + 3] = px[i * 4 + 3] / 255.0f;
  }

  stbi_image_free(px);
  return img;
}

Image fitWithin(const Image& src, std::uint32_t max_side) {
  if (src.width <= max_side && src.height <= max_side) return src;

  const float scale = std::max(static_cast<float>(src.width),
                               static_cast<float>(src.height)) /
                      static_cast<float>(max_side);
  const std::uint32_t newW = std::max(1u, static_cast<std::uint32_t>(src.width  / scale));
  const std::uint32_t newH = std::max(1u, static_cast<std::uint32_t>(src.height / scale));

  Image dst;
  dst.width  = newW;
  dst.height = newH;
  dst.rgba.resize(static_cast<std::size_t>(newW) * newH * 4);

  for (std::uint32_t y = 0; y < newH; ++y) {
    const float sy0 = static_cast<float>(y    ) * scale;
    const float sy1 = static_cast<float>(y + 1) * scale;
    const std::uint32_t iy0 = static_cast<std::uint32_t>(sy0);
    const std::uint32_t iy1 = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(std::ceil(sy1)), src.height);

    for (std::uint32_t x = 0; x < newW; ++x) {
      const float sx0 = static_cast<float>(x    ) * scale;
      const float sx1 = static_cast<float>(x + 1) * scale;
      const std::uint32_t ix0 = static_cast<std::uint32_t>(sx0);
      const std::uint32_t ix1 = std::min<std::uint32_t>(
          static_cast<std::uint32_t>(std::ceil(sx1)), src.width);

      float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
      std::uint32_t count = 0;
      for (std::uint32_t yy = iy0; yy < iy1; ++yy) {
        for (std::uint32_t xx = ix0; xx < ix1; ++xx) {
          const std::size_t i = (static_cast<std::size_t>(yy) * src.width + xx) * 4;
          r += src.rgba[i + 0];
          g += src.rgba[i + 1];
          b += src.rgba[i + 2];
          a += src.rgba[i + 3];
          ++count;
        }
      }
      if (count == 0) count = 1;
      const std::size_t di = (static_cast<std::size_t>(y) * newW + x) * 4;
      dst.rgba[di + 0] = r / count;
      dst.rgba[di + 1] = g / count;
      dst.rgba[di + 2] = b / count;
      dst.rgba[di + 3] = a / count;
    }
  }
  return dst;
}

}  // namespace broadstroke::analyzer
