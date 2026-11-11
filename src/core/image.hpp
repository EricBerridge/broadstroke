// 2D planes of values, image file I/O, and the handful of filters the engine
// needs (area resize, Gaussian blur, bilinear sampling).
#pragma once

#include "core/common.hpp"
#include "core/parallel.hpp"

namespace bs {

template <class T>
struct Plane {
  int w = 0, h = 0;
  std::vector<T> v;

  Plane() = default;
  Plane(int w_, int h_, const T& fill = T{}) : w(w_), h(h_), v(static_cast<size_t>(w_) * h_, fill) {}

  T& operator()(int x, int y) { return v[static_cast<size_t>(y) * w + x]; }
  const T& operator()(int x, int y) const { return v[static_cast<size_t>(y) * w + x]; }
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
  // Clamp-to-edge read.
  const T& clamped(int x, int y) const {
    x = x < 0 ? 0 : (x >= w ? w - 1 : x);
    y = y < 0 ? 0 : (y >= h ? h - 1 : y);
    return v[static_cast<size_t>(y) * w + x];
  }
  void fill(const T& val) { std::fill(v.begin(), v.end(), val); }
  bool empty() const { return v.empty(); }
};

using Image = Plane<Rgb>;

// Bilinear sample at continuous pixel coordinates, where the center of pixel
// (i, j) is at (i + 0.5, j + 0.5). Clamps at the border.
template <class T>
T sample_bilinear(const Plane<T>& p, float x, float y) {
  float fx = x - 0.5f, fy = y - 0.5f;
  int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
  float tx = fx - x0, ty = fy - y0;
  const T& a = p.clamped(x0, y0);
  const T& b = p.clamped(x0 + 1, y0);
  const T& c = p.clamped(x0, y0 + 1);
  const T& d = p.clamped(x0 + 1, y0 + 1);
  return (a * (1.f - tx) + b * tx) * (1.f - ty) + (c * (1.f - tx) + d * tx) * ty;
}

// Separable Gaussian blur, clamp-to-edge.
template <class T>
void gaussian_blur(Plane<T>& p, float sigma) {
  if (sigma < 0.2f || p.empty()) return;
  int r = std::max(1, static_cast<int>(std::ceil(sigma * 3.f)));
  std::vector<float> k(static_cast<size_t>(2 * r + 1));
  float sum = 0.f;
  for (int i = -r; i <= r; ++i) sum += (k[i + r] = std::exp(-0.5f * sqr(i / sigma)));
  for (float& kv : k) kv /= sum;
  Plane<T> tmp(p.w, p.h);
  parallel_for(0, p.h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < p.w; ++x) {
        T acc{};
        for (int i = -r; i <= r; ++i) {
          int xx = std::clamp(x + i, 0, p.w - 1);
          acc = acc + p(xx, y) * k[i + r];
        }
        tmp(x, y) = acc;
      }
  }, 8);
  parallel_for(0, p.h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < p.w; ++x) {
        T acc{};
        for (int i = -r; i <= r; ++i) {
          int yy = std::clamp(y + i, 0, p.h - 1);
          acc = acc + tmp(x, yy) * k[i + r];
        }
        p(x, y) = acc;
      }
  }, 8);
}

// Area-averaging resize (box filter over the source footprint). Good for
// shrinking; for enlarging it degrades to bilinear-like interpolation.
template <class T>
Plane<T> resize_area(const Plane<T>& src, int w, int h) {
  Plane<T> dst(w, h);
  if (src.empty() || w <= 0 || h <= 0) return dst;
  float sx = static_cast<float>(src.w) / w, sy = static_cast<float>(src.h) / h;
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y) {
      float fy0 = y * sy, fy1 = (y + 1) * sy;
      for (int x = 0; x < w; ++x) {
        float fx0 = x * sx, fx1 = (x + 1) * sx;
        if (sx <= 1.f || sy <= 1.f) {
          dst(x, y) = sample_bilinear(src, (fx0 + fx1) * 0.5f, (fy0 + fy1) * 0.5f);
          continue;
        }
        T acc{};
        float wsum = 0.f;
        int iy0 = static_cast<int>(std::floor(fy0)), iy1 = static_cast<int>(std::ceil(fy1));
        int ix0 = static_cast<int>(std::floor(fx0)), ix1 = static_cast<int>(std::ceil(fx1));
        for (int yy = iy0; yy < iy1; ++yy) {
          float wy = std::min(fy1, yy + 1.f) - std::max(fy0, static_cast<float>(yy));
          for (int xx = ix0; xx < ix1; ++xx) {
            float wx = std::min(fx1, xx + 1.f) - std::max(fx0, static_cast<float>(xx));
            float wgt = wx * wy;
            acc = acc + src.clamped(xx, yy) * wgt;
            wsum += wgt;
          }
        }
        dst(x, y) = acc * (1.f / wsum);
      }
    }
  }, 4);
  return dst;
}

// sRGB transfer functions.
inline float srgb_to_linear(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linear_to_srgb(float c) {
  c = clamp01(c);
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

// Loads JPG/PNG/BMP into linear RGB. Photographs are untrusted input: the
// header is validated and the pixel count capped before any decode
// allocation. Returns an empty image and sets *error on failure.
Image load_image(const std::string& path, std::string* error = nullptr);

// Writes linear RGB as 8-bit sRGB. Format by extension: .png, .jpg/.jpeg, .bmp.
bool save_image(const std::string& path, const Image& img, std::string* error = nullptr);

// Packs linear RGB into 8-bit sRGB bytes (3 per pixel, row-major).
std::vector<uint8_t> to_srgb8(const Image& img);

}  // namespace bs
