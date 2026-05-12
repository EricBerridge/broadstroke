#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace broadstroke::reference {

// CPU mirrors of the three pass-based compose kernels.
//
//   substrate_to_rgba: substrate heightfield → rgba (initial canvas).
//   apply_graphite:    rgba += graphite layer (Lambert blend).
//   apply_pigment:     rgba := KM_blend(rgba, pigment layer).
//
// Each function expects buffers sized exactly W*H (or W*H*4 for rgba).

struct SubstrateParams {
  std::array<float, 3> base_color;
  std::array<float, 3> light_dir;     // pre-normalized
  float ambient;
  float height_scale;
};

std::vector<float> substrate_to_rgba(std::span<const float> heightfield,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     const SubstrateParams& params);

struct GraphiteParams {
  std::array<float, 3> light_dir;     // pre-normalized
  float ambient;
  float height_scale;
  std::array<float, 3> graphite_color;
};

void apply_graphite(std::span<float> rgba,
                    std::span<const float> graphite,
                    std::span<const float> heightfield,
                    std::uint32_t width,
                    std::uint32_t height,
                    const GraphiteParams& params);

struct PigmentParams {
  std::array<float, 3> K;
  std::array<float, 3> S;
  float thickness_scale;
};

void apply_pigment(std::span<float> rgba,
                   std::span<const float> pigment,
                   std::uint32_t width,
                   std::uint32_t height,
                   const PigmentParams& params);

}  // namespace broadstroke::reference
