#include "broadstroke/analyzer/flow.hpp"

#include <algorithm>
#include <cmath>

namespace broadstroke::analyzer {

namespace {

// Separable box blur on a single channel. `r` is the half-width.
std::vector<float> boxBlur(const std::vector<float>& src,
                           std::uint32_t w, std::uint32_t h, int r) {
  if (r <= 0) return src;
  std::vector<float> tmp(src.size());
  std::vector<float> out(src.size());
  const float inv = 1.0f / static_cast<float>(2 * r + 1);

  // Horizontal.
  for (std::uint32_t y = 0; y < h; ++y) {
    float sum = 0.0f;
    for (int k = -r; k <= r; ++k) {
      const int x = std::clamp(k, 0, static_cast<int>(w) - 1);
      sum += src[static_cast<std::size_t>(y) * w + x];
    }
    for (std::uint32_t x = 0; x < w; ++x) {
      tmp[static_cast<std::size_t>(y) * w + x] = sum * inv;
      const int x_add = std::clamp(static_cast<int>(x) + r + 1, 0, static_cast<int>(w) - 1);
      const int x_sub = std::clamp(static_cast<int>(x) - r,     0, static_cast<int>(w) - 1);
      sum += src[static_cast<std::size_t>(y) * w + x_add] -
             src[static_cast<std::size_t>(y) * w + x_sub];
    }
  }
  // Vertical.
  for (std::uint32_t x = 0; x < w; ++x) {
    float sum = 0.0f;
    for (int k = -r; k <= r; ++k) {
      const int y = std::clamp(k, 0, static_cast<int>(h) - 1);
      sum += tmp[static_cast<std::size_t>(y) * w + x];
    }
    for (std::uint32_t y = 0; y < h; ++y) {
      out[static_cast<std::size_t>(y) * w + x] = sum * inv;
      const int y_add = std::clamp(static_cast<int>(y) + r + 1, 0, static_cast<int>(h) - 1);
      const int y_sub = std::clamp(static_cast<int>(y) - r,     0, static_cast<int>(h) - 1);
      sum += tmp[static_cast<std::size_t>(y_add) * w + x] -
             tmp[static_cast<std::size_t>(y_sub) * w + x];
    }
  }
  return out;
}

}  // namespace

FlowField structureTensorFlow(const SobelGradient& g,
                              std::uint32_t width, std::uint32_t height,
                              int smooth_radius) {
  const std::size_t n = static_cast<std::size_t>(width) * height;
  std::vector<float> gxx(n), gyy(n), gxy(n);
  for (std::size_t i = 0; i < n; ++i) {
    gxx[i] = g.gx[i] * g.gx[i];
    gyy[i] = g.gy[i] * g.gy[i];
    gxy[i] = g.gx[i] * g.gy[i];
  }
  gxx = boxBlur(gxx, width, height, smooth_radius);
  gyy = boxBlur(gyy, width, height, smooth_radius);
  gxy = boxBlur(gxy, width, height, smooth_radius);

  FlowField f;
  f.dx.assign(n, 0.0f);
  f.dy.assign(n, 0.0f);

  for (std::size_t i = 0; i < n; ++i) {
    // 2x2 symmetric eigendecomposition. Eigenvalues:
    //   λ = (a+d)/2 ± √(((a-d)/2)² + b²)
    // We want the eigenvector for the SMALLER eigenvalue — that's the
    // direction of least intensity variation, i.e., along the edge.
    const float a = gxx[i];
    const float d = gyy[i];
    const float b = gxy[i];
    const float trace_half = 0.5f * (a + d);
    const float diff_half  = 0.5f * (a - d);
    const float root = std::sqrt(diff_half * diff_half + b * b);
    const float lambda_min = trace_half - root;

    // Eigenvector for λ_min: (b, λ_min - a). Falls back to swapping the
    // form when the first component is small to avoid all-zero outputs
    // on horizontal-gradient pixels.
    float vx, vy;
    if (std::fabs(b) > std::fabs(lambda_min - a)) {
      vx = lambda_min - d;
      vy = b;
    } else {
      vx = b;
      vy = lambda_min - a;
    }
    const float len = std::sqrt(vx * vx + vy * vy);
    if (len > 1e-6f) {
      f.dx[i] = vx / len;
      f.dy[i] = vy / len;
    }
    // else: leave (0, 0) — the planner treats zero-flow as "no preferred
    // direction" and falls back to a default angle.
  }
  return f;
}

}  // namespace broadstroke::analyzer
