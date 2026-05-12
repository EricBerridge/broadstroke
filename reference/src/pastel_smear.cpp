#include "broadstroke/reference/pastel_smear.hpp"

#include <algorithm>
#include <cmath>

namespace broadstroke::reference {

namespace {

inline float fetch(std::span<const float> in,
                   std::uint32_t w, std::uint32_t h, int x, int y) {
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (static_cast<std::uint32_t>(x) >= w) x = static_cast<int>(w) - 1;
  if (static_cast<std::uint32_t>(y) >= h) y = static_cast<int>(h) - 1;
  return in[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)];
}

}  // namespace

void pastel_smear(std::span<float> pigment_out,
                  std::span<const float> pigment_in,
                  std::uint32_t canvas_width,
                  std::uint32_t canvas_height,
                  const PastelSmearParams& params) {
  const std::uint32_t x_end = std::min(
      canvas_width,
      params.offset_x + (params.extent_w ? params.extent_w
                                         : canvas_width  - params.offset_x));
  const std::uint32_t y_end = std::min(
      canvas_height,
      params.offset_y + (params.extent_h ? params.extent_h
                                         : canvas_height - params.offset_y));

  const float abx = params.bx - params.ax;
  const float aby = params.by - params.ay;
  const float len = std::sqrt(abx * abx + aby * aby);

  for (std::uint32_t y = params.offset_y; y < y_end; ++y) {
    for (std::uint32_t x = params.offset_x; x < x_end; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * canvas_width + x;
      if (len < 1e-3f) {
        pigment_out[i] = pigment_in[i];
        continue;
      }
      const float dx = abx / len;
      const float dy = aby / len;
      const float perpx = -dy;
      const float perpy =  dx;
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      const float relx = px - params.ax;
      const float rely = py - params.ay;
      const float along  = relx * dx    + rely * dy;
      const float across = std::fabs(relx * perpx + rely * perpy);

      if (across >= params.smear_radius_px ||
          along  < -params.smear_radius_px ||
          along  >  len + params.smear_radius_px) {
        pigment_out[i] = pigment_in[i];
        continue;
      }

      const float band = std::max(0.01f,
                                   params.smear_softness * params.smear_radius_px);
      const float u = std::clamp((params.smear_radius_px - across) / band, 0.0f, 1.0f);
      const float perp_falloff = u * u * (3.0f - 2.0f * u);

      const int taps = static_cast<int>(std::max<std::uint32_t>(1, params.smear_taps));
      float acc = 0.0f;
      for (int k = -taps; k <= taps; ++k) {
        const float t = static_cast<float>(k);
        // Match the GPU kernel's floor(x + 0.5) — see comment there.
        const int sx = static_cast<int>(std::floor(px + t * dx + 0.5f));
        const int sy = static_cast<int>(std::floor(py + t * dy + 0.5f));
        acc += fetch(pigment_in, canvas_width, canvas_height, sx, sy);
      }
      const float smeared = acc / static_cast<float>(2 * taps + 1);

      const float strength = params.smear_strength * perp_falloff;
      pigment_out[i] = pigment_in[i] + (smeared - pigment_in[i]) * strength;
    }
  }
}

}  // namespace broadstroke::reference
