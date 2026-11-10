#include "broadstroke/reference/compose.hpp"

#include <algorithm>
#include <cmath>

namespace broadstroke::reference {

namespace {

inline std::array<float, 3> normalize3(std::array<float, 3> v) {
  const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len == 0.0f) return {0.0f, 0.0f, 1.0f};
  return {v[0] / len, v[1] / len, v[2] / len};
}

inline float clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Shared raking-light shading factor in [0, 1]. Same math the GPU passes
// run; factored out so substrate and graphite stay in sync.
inline float relief_shading(std::span<const float> heightfield,
                            std::uint32_t width, std::uint32_t height,
                            std::uint32_t x, std::uint32_t y,
                            const std::array<float, 3>& L,
                            float ambient, float height_scale) {
  const std::uint32_t xm = (x == 0)            ? 0          : x - 1;
  const std::uint32_t xp = (x + 1 >= width)    ? width - 1  : x + 1;
  const std::uint32_t ym = (y == 0)            ? 0          : y - 1;
  const std::uint32_t yp = (y + 1 >= height)   ? height - 1 : y + 1;

  const float h_left  = heightfield[static_cast<std::size_t>(y)  * width + xm];
  const float h_right = heightfield[static_cast<std::size_t>(y)  * width + xp];
  const float h_down  = heightfield[static_cast<std::size_t>(ym) * width + x ];
  const float h_up    = heightfield[static_cast<std::size_t>(yp) * width + x ];

  const float gx = (h_right - h_left) * 0.5f * height_scale;
  const float gy = (h_up    - h_down) * 0.5f * height_scale;
  const auto N = normalize3({-gx, -gy, 1.0f});
  const float diffuse = std::max(0.0f, N[0]*L[0] + N[1]*L[1] + N[2]*L[2]);
  return ambient + (1.0f - ambient) * diffuse;
}

}  // namespace

std::vector<float> substrate_to_rgba(std::span<const float> heightfield,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     const SubstrateParams& params) {
  const std::size_t n = static_cast<std::size_t>(width) * height;
  std::vector<float> rgba(n * 4, 0.0f);
  const auto L = normalize3(params.light_dir);

  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const float shading = relief_shading(heightfield, width, height,
                                           x, y, L, params.ambient,
                                           params.height_scale);
      const std::size_t i = static_cast<std::size_t>(y) * width + x;
      const std::size_t ix = i * 4;
      rgba[ix + 0] = params.base_color[0] * shading;
      rgba[ix + 1] = params.base_color[1] * shading;
      rgba[ix + 2] = params.base_color[2] * shading;
      rgba[ix + 3] = 1.0f;
    }
  }
  return rgba;
}

void apply_graphite(std::span<float> rgba,
                    std::span<const float> graphite,
                    std::span<const float> heightfield,
                    std::uint32_t width,
                    std::uint32_t height,
                    const GraphiteParams& params) {
  const auto L = normalize3(params.light_dir);

  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const float shading = relief_shading(heightfield, width, height,
                                           x, y, L, params.ambient,
                                           params.height_scale);
      const std::size_t i  = static_cast<std::size_t>(y) * width + x;
      const std::size_t ix = i * 4;
      const float g = clamp01(graphite[i]);
      const float opacity = std::min(0.96f, 0.92f * std::pow(g, 0.90f));
      const float lit = 0.52f + 0.42f * shading;
      const float gR = params.graphite_color[0] * lit;
      const float gG = params.graphite_color[1] * lit;
      const float gB = params.graphite_color[2] * lit;
      rgba[ix + 0] = rgba[ix + 0] + (gR - rgba[ix + 0]) * opacity;
      rgba[ix + 1] = rgba[ix + 1] + (gG - rgba[ix + 1]) * opacity;
      rgba[ix + 2] = rgba[ix + 2] + (gB - rgba[ix + 2]) * opacity;
    }
  }
}

void apply_pigment(std::span<float> rgba,
                   std::span<const float> pigment,
                   std::uint32_t width,
                   std::uint32_t height,
                   const PigmentParams& params) {
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const std::size_t i  = static_cast<std::size_t>(y) * width + x;
      const std::size_t ix = i * 4;
      const float c = std::max(pigment[i], 0.0f);
      if (c <= 0.0f) continue;

      for (int ch = 0; ch < 3; ++ch) {
        const float K_eff = params.K[ch] * c;
        const float S_eff = params.S[ch] * c + 1e-4f;
        const float ratio = K_eff / S_eff;
        const float root  = std::sqrt(ratio * ratio + 2.0f * ratio);
        const float R_inf = 1.0f + ratio - root;

        const float extinction = (K_eff + S_eff) * params.thickness_scale;
        const float T = std::exp(-extinction);
        const float prev = rgba[ix + ch];
        rgba[ix + ch] = R_inf * (1.0f - T) + prev * T;
      }
    }
  }
}

}  // namespace broadstroke::reference
