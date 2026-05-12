// GPU-vs-CPU equivalence test for compute/kernels/support_visualize.comp.
// Generates a small support field deterministically, dispatches both
// implementations, and asserts agreement within float tolerance.

#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/reference/support_visualize.hpp"
#include "broadstroke/support/generate.hpp"
#include "broadstroke/support/preset.hpp"
#include "broadstroke/support/support_data.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr std::uint32_t kW       = 128;
constexpr std::uint32_t kH       = 96;
constexpr std::uint32_t kLocalX  = 16;
constexpr std::uint32_t kLocalY  = 16;
constexpr float kTolerance       = 1e-5f;

broadstroke::support::Preset testPreset() {
  broadstroke::support::Preset p;
  p.name = "test";
  p.kind = "cold_press";
  p.base_color = {0.9f, 0.85f, 0.8f};
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
    data.width  = kW;
    data.height = kH;
    broadstroke::support::generate(data, preset, /*seed=*/12345);

    broadstroke::reference::VisualizeParams params{
        /*base_color=*/{preset.base_color[0], preset.base_color[1], preset.base_color[2]},
        /*light_dir=*/{0.5f, 0.5f, 0.7071f},  // already roughly normalized
        /*ambient=*/0.6f,
        /*height_scale=*/8.0f,
    };

    // GPU.
    broadstroke::compute::Context ctx;
    auto heightfield = ctx.upload(data.heightfield);
    auto output = ctx.upload(
        std::vector<float>(static_cast<std::size_t>(kW) * kH * 4, 0.0f));
    std::vector<float> packed = {
        static_cast<float>(kW),
        static_cast<float>(kH),
        params.base_color[0], params.base_color[1], params.base_color[2],
        params.light_dir[0],  params.light_dir[1],  params.light_dir[2],
        params.ambient, params.height_scale,
    };
    auto paramsBuf = ctx.upload(packed);

    const std::uint32_t gx = (kW + kLocalX - 1) / kLocalX;
    const std::uint32_t gy = (kH + kLocalY - 1) / kLocalY;
    ctx.dispatch("support_visualize",
                 {&heightfield, &output, &paramsBuf},
                 {gx, gy, 1});
    auto gpu = output.download();

    // CPU.
    auto cpu = broadstroke::reference::support_visualize(
        data.heightfield, kW, kH, params);

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
