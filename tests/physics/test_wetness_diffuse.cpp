// GPU-vs-CPU equivalence for wetness_diffuse. We initialize a blob of
// pigment + water at the center of a small canvas, run one diffusion
// step on each, and assert the resulting fields match.

#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/reference/wetness_diffuse.hpp"

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

}  // namespace

int main() {
  try {
    // Center blob of pigment + wetness; constant absorption ramp.
    std::vector<float> pig (static_cast<std::size_t>(kW) * kH, 0.0f);
    std::vector<float> wet (static_cast<std::size_t>(kW) * kH, 0.0f);
    std::vector<float> absorption(static_cast<std::size_t>(kW) * kH, 0.5f);
    const float cx = kW * 0.5f, cy = kH * 0.5f;
    for (std::uint32_t y = 0; y < kH; ++y) {
      for (std::uint32_t x = 0; x < kW; ++x) {
        const float dx = static_cast<float>(x) - cx;
        const float dy = static_cast<float>(y) - cy;
        const float d  = std::sqrt(dx * dx + dy * dy);
        const std::size_t i = static_cast<std::size_t>(y) * kW + x;
        if (d < 10.0f) {
          pig[i] = 1.0f - d / 10.0f;
          wet[i] = 0.9f * (1.0f - d / 10.0f);
        }
        // Absorption gradient catches per-pixel-absorption indexing bugs.
        absorption[i] = 0.3f + 0.5f * (static_cast<float>(x) / (kW - 1));
      }
    }

    broadstroke::reference::WetnessDiffuseParams params{
        /*water_diffusion=*/0.20f,
        /*pigment_diffusion=*/0.18f,
        /*dry_rate=*/0.08f,
        /*dt=*/1.0f,
    };

    broadstroke::compute::Context ctx;
    auto pin_g  = ctx.upload(pig);
    auto pout_g = ctx.upload(pig);  // initial contents irrelevant
    auto win_g  = ctx.upload(wet);
    auto wout_g = ctx.upload(wet);
    auto abs_g  = ctx.upload(absorption);

    std::vector<float> packedParams = {
        static_cast<float>(kW), static_cast<float>(kH),
        params.water_diffusion, params.pigment_diffusion,
        params.dry_rate, params.dt,
        0.0f, 0.0f,
    };
    auto params_g = ctx.upload(packedParams);

    const std::uint32_t gx = (kW + kLocalX - 1) / kLocalX;
    const std::uint32_t gy = (kH + kLocalY - 1) / kLocalY;
    ctx.dispatch("wetness_diffuse",
                 {&pin_g, &pout_g, &win_g, &wout_g, &abs_g, &params_g},
                 {gx, gy, 1});
    auto gpu_p = pout_g.download();
    auto gpu_w = wout_g.download();

    std::vector<float> cpu_p(static_cast<std::size_t>(kW) * kH, 0.0f);
    std::vector<float> cpu_w(static_cast<std::size_t>(kW) * kH, 0.0f);
    broadstroke::reference::wetness_diffuse(
        cpu_p, cpu_w, pig, wet, absorption, kW, kH, params);

    for (std::size_t i = 0; i < gpu_p.size(); ++i) {
      if (std::fabs(gpu_p[i] - cpu_p[i]) > kTolerance) {
        std::fprintf(stderr, "FAIL pigment idx=%zu gpu=%.6f cpu=%.6f\n", i,
                     (double)gpu_p[i], (double)cpu_p[i]);
        return 1;
      }
      if (std::fabs(gpu_w[i] - cpu_w[i]) > kTolerance) {
        std::fprintf(stderr, "FAIL wetness idx=%zu gpu=%.6f cpu=%.6f\n", i,
                     (double)gpu_w[i], (double)cpu_w[i]);
        return 1;
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
