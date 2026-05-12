#include "broadstroke/reference/wetness_diffuse.hpp"

#include <algorithm>

namespace broadstroke::reference {

void wetness_diffuse(std::span<float> pigment_out,
                     std::span<float> wetness_out,
                     std::span<const float> pigment_in,
                     std::span<const float> wetness_in,
                     std::span<const float> absorption,
                     std::uint32_t canvas_width,
                     std::uint32_t canvas_height,
                     const WetnessDiffuseParams& params) {
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
      const std::uint32_t xm = (x == 0)                     ? 0                  : x - 1;
      const std::uint32_t xp = (x + 1 >= canvas_width)      ? canvas_width  - 1  : x + 1;
      const std::uint32_t ym = (y == 0)                     ? 0                  : y - 1;
      const std::uint32_t yp = (y + 1 >= canvas_height)     ? canvas_height - 1  : y + 1;

      const std::size_t i  = static_cast<std::size_t>(y)  * canvas_width + x;
      const std::size_t iL = static_cast<std::size_t>(y)  * canvas_width + xm;
      const std::size_t iR = static_cast<std::size_t>(y)  * canvas_width + xp;
      const std::size_t iD = static_cast<std::size_t>(ym) * canvas_width + x;
      const std::size_t iU = static_cast<std::size_t>(yp) * canvas_width + x;

      const float w_here = wetness_in[i];
      const float w_L = wetness_in[iL]; const float w_R = wetness_in[iR];
      const float w_D = wetness_in[iD]; const float w_U = wetness_in[iU];
      const float lap_w = (w_L + w_R + w_D + w_U) - 4.0f * w_here;

      const float p_here = pigment_in[i];
      const float p_L = pigment_in[iL]; const float p_R = pigment_in[iR];
      const float p_D = pigment_in[iD]; const float p_U = pigment_in[iU];
      const float lap_p = (p_L + p_R + p_D + p_U) - 4.0f * p_here;

      const float avg_neighbor_w = 0.25f * (w_L + w_R + w_D + w_U);
      const float carrier = std::clamp(std::min(w_here, avg_neighbor_w), 0.0f, 1.0f);

      float new_w = w_here + params.dt * params.water_diffusion   * lap_w
                           - params.dt * params.dry_rate          * absorption[i];
      float new_p = p_here + params.dt * params.pigment_diffusion * carrier * lap_p;

      wetness_out[i] = std::max(new_w, 0.0f);
      pigment_out[i] = std::max(new_p, 0.0f);
    }
  }
}

}  // namespace broadstroke::reference
