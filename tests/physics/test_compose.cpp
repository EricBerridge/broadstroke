// GPU-vs-CPU equivalence for the pass-based compose kernels:
// substrate_to_rgba, apply_graphite, apply_pigment. Runs all three in
// order on a test substrate + graphite + pigment field and asserts the
// final RGBA matches the CPU reference within tolerance.

#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/reference/compose.hpp"
#include "broadstroke/support/generate.hpp"
#include "broadstroke/support/preset.hpp"
#include "broadstroke/support/support_data.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr std::uint32_t kW       = 96;
constexpr std::uint32_t kH       = 64;
constexpr std::uint32_t kLocalX  = 16;
constexpr std::uint32_t kLocalY  = 16;
constexpr float kTolerance       = 2e-5f;

broadstroke::support::Preset testPreset() {
  broadstroke::support::Preset p;
  p.name = "test"; p.kind = "cold_press";
  p.base_color = {0.9f, 0.85f, 0.78f};
  p.heightfield_amplitude = 0.4f;
  p.noise_base_frequency  = 0.05f;
  p.noise_octaves         = 4;
  return p;
}

}  // namespace

int main() {
  try {
    auto preset = testPreset();
    broadstroke::support::SupportData data;
    data.width = kW; data.height = kH;
    broadstroke::support::generate(data, preset, /*seed=*/4242);

    // Synthetic graphite + pigment fields with non-trivial spatial pattern.
    std::vector<float> graphite(static_cast<std::size_t>(kW) * kH, 0.0f);
    std::vector<float> pigment (static_cast<std::size_t>(kW) * kH, 0.0f);
    const float cx = kW * 0.5f, cy = kH * 0.5f;
    const float rmax = std::sqrt(cx * cx + cy * cy);
    for (std::uint32_t y = 0; y < kH; ++y) {
      for (std::uint32_t x = 0; x < kW; ++x) {
        const float dx = static_cast<float>(x) - cx;
        const float dy = static_cast<float>(y) - cy;
        const float r  = std::sqrt(dx*dx + dy*dy) / rmax;
        const std::size_t i = static_cast<std::size_t>(y) * kW + x;
        graphite[i] = 0.6f * (1.0f - r);            // graphite peaks at center
        pigment [i] = 1.4f * r;                      // pigment peaks at edge
      }
    }

    const std::array<float, 3> light_dir = {0.5f, 0.5f, 0.7071f};
    const float ambient = 0.7f;
    const float height_scale = 9.0f;

    broadstroke::reference::SubstrateParams sub_p{
        {preset.base_color[0], preset.base_color[1], preset.base_color[2]},
        light_dir, ambient, height_scale};
    broadstroke::reference::GraphiteParams grp{
        light_dir, ambient, height_scale, {0.08f, 0.07f, 0.07f}};
    broadstroke::reference::PigmentParams pgp{
        {0.85f, 0.45f, 0.05f}, {0.38f, 0.40f, 0.55f}, /*thickness_scale=*/2.0f};

    // GPU: substrate → apply_graphite → apply_pigment.
    broadstroke::compute::Context ctx;
    auto heightfield_g = ctx.upload(data.heightfield);
    auto graphite_g    = ctx.upload(graphite);
    auto pigment_g     = ctx.upload(pigment);

    std::vector<float> rgba_zero(static_cast<std::size_t>(kW) * kH * 4, 0.0f);
    auto rgba_g = ctx.upload(rgba_zero);

    const std::uint32_t gx = (kW + kLocalX - 1) / kLocalX;
    const std::uint32_t gy = (kH + kLocalY - 1) / kLocalY;

    {
      std::vector<float> params = {
          static_cast<float>(kW), static_cast<float>(kH),
          sub_p.base_color[0], sub_p.base_color[1], sub_p.base_color[2],
          sub_p.light_dir[0],  sub_p.light_dir[1],  sub_p.light_dir[2],
          sub_p.ambient,       sub_p.height_scale,
      };
      auto pb = ctx.upload(params);
      ctx.dispatch("substrate_to_rgba",
                   {&heightfield_g, &rgba_g, &pb}, {gx, gy, 1});
    }
    {
      std::vector<float> params = {
          static_cast<float>(kW), static_cast<float>(kH),
          grp.light_dir[0], grp.light_dir[1], grp.light_dir[2],
          grp.ambient, grp.height_scale,
          grp.graphite_color[0], grp.graphite_color[1], grp.graphite_color[2],
      };
      auto pb = ctx.upload(params);
      ctx.dispatch("apply_graphite",
                   {&rgba_g, &graphite_g, &heightfield_g, &pb}, {gx, gy, 1});
    }
    {
      std::vector<float> params = {
          static_cast<float>(kW), static_cast<float>(kH),
          pgp.K[0], pgp.K[1], pgp.K[2],
          pgp.S[0], pgp.S[1], pgp.S[2],
          pgp.thickness_scale,
      };
      auto pb = ctx.upload(params);
      ctx.dispatch("apply_pigment",
                   {&rgba_g, &pigment_g, &pb}, {gx, gy, 1});
    }
    auto gpu = rgba_g.download();

    // CPU mirror.
    auto cpu = broadstroke::reference::substrate_to_rgba(
        data.heightfield, kW, kH, sub_p);
    broadstroke::reference::apply_graphite(cpu, graphite, data.heightfield,
                                           kW, kH, grp);
    broadstroke::reference::apply_pigment(cpu, pigment, kW, kH, pgp);

    if (gpu.size() != cpu.size()) {
      std::fprintf(stderr, "FAIL: size mismatch gpu=%zu cpu=%zu\n",
                   gpu.size(), cpu.size());
      return 1;
    }
    for (std::size_t i = 0; i < gpu.size(); ++i) {
      if (std::fabs(gpu[i] - cpu[i]) > kTolerance) {
        std::fprintf(stderr, "FAIL: index %zu gpu=%.6f cpu=%.6f\n", i,
                     static_cast<double>(gpu[i]),
                     static_cast<double>(cpu[i]));
        return 1;
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
