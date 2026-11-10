#include "broadstroke/reference/pencil_stroke.hpp"

#include <algorithm>
#include <cmath>

namespace broadstroke::reference {

void pencil_stroke(std::span<float> graphite,
                   std::span<const float> tooth,
                   std::span<const float> stroke,
                   std::uint32_t canvas_width,
                   std::uint32_t canvas_height,
                   const PencilStrokeParams& params) {
  const std::size_t num_points = stroke.size() / 3;
  if (num_points < 2) return;

  const float band = std::max(0.01f, params.tip_softness * params.tip_radius_px);

  const std::uint32_t x_end = std::min(
      canvas_width,
      params.offset_x + (params.extent_w ? params.extent_w
                                         : canvas_width  - params.offset_x));
  const std::uint32_t y_end = std::min(
      canvas_height,
      params.offset_y + (params.extent_h ? params.extent_h
                                         : canvas_height - params.offset_y));

  for (std::uint32_t y = params.offset_y; y < y_end; ++y) {
    for (std::uint32_t x = params.offset_x; x < x_end; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * canvas_width + x;
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      const float tooth_here = 0.16f + 0.90f * tooth[i];

      float total_deposit = 0.0f;

      for (std::size_t s = 0; s + 1 < num_points; ++s) {
        const std::size_t i0 = s * 3;
        const std::size_t i1 = (s + 1) * 3;
        const float ax = stroke[i0 + 0];
        const float ay = stroke[i0 + 1];
        const float pa = stroke[i0 + 2];
        const float bx = stroke[i1 + 0];
        const float by = stroke[i1 + 1];
        const float pb = stroke[i1 + 2];

        const float abx = bx - ax;
        const float aby = by - ay;
        const float len_sq = abx * abx + aby * aby;
        const float dot_v  = (px - ax) * abx + (py - ay) * aby;
        const float t = (len_sq > 1e-8f)
                          ? std::clamp(dot_v / len_sq, 0.0f, 1.0f)
                          : 0.0f;

        const float cx = ax + t * abx;
        const float cy = ay + t * aby;
        const float dx = px - cx;
        const float dy = py - cy;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist >= params.tip_radius_px) continue;

        const float u = std::clamp(
            (params.tip_radius_px - dist) / band, 0.0f, 1.0f);
        const float falloff = u * u * (3.0f - 2.0f * u);
        const float pressure = pa + t * (pb - pa);
        total_deposit += pressure * falloff * tooth_here * params.deposit_strength;
      }

      const float prev = graphite[i];
      graphite[i] = 1.0f - (1.0f - prev) * std::exp(-total_deposit);
    }
  }
}

}  // namespace broadstroke::reference
