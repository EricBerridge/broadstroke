#include "broadstroke/support/generate.hpp"

#include "noise.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace broadstroke::support {

namespace {

inline float clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

}  // namespace

void generate(SupportData& out, const Preset& preset, std::uint64_t seed) {
  const std::uint32_t w = out.width;
  const std::uint32_t h = out.height;
  const std::size_t n = static_cast<std::size_t>(w) * h;

  out.heightfield.assign(n, 0.0f);
  out.absorption.assign(n, preset.absorption_base);
  out.wetness.assign(n, 0.0f);
  out.grain_x.assign(n, 0.0f);
  out.grain_y.assign(n, 0.0f);
  out.tooth.assign(n, 0.0f);

  // Grain anisotropy vector. Constant across the field for now;
  // wood-grain support later will vary it per pixel.
  const float grain_rad =
      preset.grain_direction_degrees * std::numbers::pi_v<float> / 180.0f;
  const float gx_val = preset.grain_anisotropy * std::cos(grain_rad);
  const float gy_val = preset.grain_anisotropy * std::sin(grain_rad);

  // Base frequency is in cycles per pixel — premultiply once.
  const float base_freq = preset.noise_base_frequency;

  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * w + x;
      const float fx = static_cast<float>(x) * base_freq;
      const float fy = static_cast<float>(y) * base_freq;

      // FBM noise in [0, 1] → re-centered to [-1, 1].
      const float n01 = detail::fbm(fx, fy,
                                    preset.noise_octaves,
                                    preset.noise_persistence,
                                    preset.noise_lacunarity,
                                    seed);
      float height = (n01 * 2.0f - 1.0f) * preset.heightfield_amplitude;

      // Canvas weave: plain 1:1 over/under weave. Space is divided into
      // half-period cells; in a checkerboard pattern, warp threads arc over
      // weft threads (and vice versa). Each on-top thread bows upward with a
      // raised-cosine profile in its crossing direction. This produces the
      // correct alternating-bump pattern rather than a diagonal split.
      if (preset.weave_period_pixels > 0.0f && preset.weave_amplitude > 0.0f) {
        const float half = preset.weave_period_pixels * 0.5f;
        const int cx = static_cast<int>(std::floor(static_cast<float>(x) / half));
        const int cy = static_cast<int>(std::floor(static_cast<float>(y) / half));
        const bool warp_over = ((cx + cy) & 1) == 0;
        // Local position [0, 1) within the current half-period cell.
        const float lx = static_cast<float>(x) / half - static_cast<float>(cx);
        const float ly = static_cast<float>(y) / half - static_cast<float>(cy);
        // Raised cosine: 0 at both edges of the cell, 1 at the center.
        // Warp threads (running in x) arc in the y direction; weft arc in x.
        const float t = warp_over ? ly : lx;
        const float arc = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * t));
        height += (arc - 0.5f) * preset.weave_amplitude;
      }

      out.heightfield[i] = height;

      // Absorption tracks the heightfield: valleys (low height) absorb
      // slightly more, ridges shed water. Magnitude tuned for subtle
      // effect; later media will revisit this.
      out.absorption[i] = clamp01(preset.absorption_base - 0.15f * height);

      out.grain_x[i] = gx_val;
      out.grain_y[i] = gy_val;
    }
  }

  // Tooth = local gradient magnitude of the heightfield. Computed in a
  // second pass so all neighbors are valid. Edges clamp.
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      const std::uint32_t xm = (x == 0) ? 0 : x - 1;
      const std::uint32_t xp = (x + 1 >= w) ? w - 1 : x + 1;
      const std::uint32_t ym = (y == 0) ? 0 : y - 1;
      const std::uint32_t yp = (y + 1 >= h) ? h - 1 : y + 1;

      const float dx = out.heightfield[static_cast<std::size_t>(y) * w + xp] -
                       out.heightfield[static_cast<std::size_t>(y) * w + xm];
      const float dy = out.heightfield[static_cast<std::size_t>(yp) * w + x] -
                       out.heightfield[static_cast<std::size_t>(ym) * w + x];
      out.tooth[static_cast<std::size_t>(y) * w + x] =
          0.5f * std::sqrt(dx * dx + dy * dy);
    }
  }
}

}  // namespace broadstroke::support
