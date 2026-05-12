// GPU-vs-CPU equivalence for pastel_smear.

#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/reference/pastel_smear.hpp"

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
    // Sharp blob of pigment at one end of the smear path.
    std::vector<float> pig(static_cast<std::size_t>(kW) * kH, 0.0f);
    for (std::uint32_t y = 0; y < kH; ++y) {
      for (std::uint32_t x = 0; x < kW; ++x) {
        const float dx = static_cast<float>(x) - 22.0f;
        const float dy = static_cast<float>(y) - 32.0f;
        const float d  = std::sqrt(dx * dx + dy * dy);
        if (d < 8.0f) pig[static_cast<std::size_t>(y) * kW + x] = 1.5f * (1.0f - d / 8.0f);
      }
    }

    broadstroke::reference::PastelSmearParams params{
        /*ax=*/22.0f, /*ay=*/32.0f, /*bx=*/74.0f, /*by=*/32.0f,
        /*smear_radius_px=*/10.0f,
        /*smear_softness=*/0.7f,
        /*smear_taps=*/6,
        /*smear_strength=*/0.6f,
    };

    broadstroke::compute::Context ctx;
    auto pin_g  = ctx.upload(pig);
    auto pout_g = ctx.upload(pig);

    std::vector<float> packedParams = {
        static_cast<float>(kW), static_cast<float>(kH),
        params.ax, params.ay, params.bx, params.by,
        params.smear_radius_px, params.smear_softness,
        static_cast<float>(params.smear_taps), params.smear_strength,
        0.0f, 0.0f,
    };
    auto params_g = ctx.upload(packedParams);

    const std::uint32_t gx = (kW + kLocalX - 1) / kLocalX;
    const std::uint32_t gy = (kH + kLocalY - 1) / kLocalY;
    ctx.dispatch("pastel_smear",
                 {&pin_g, &pout_g, &params_g}, {gx, gy, 1});
    auto gpu = pout_g.download();

    std::vector<float> cpu(static_cast<std::size_t>(kW) * kH, 0.0f);
    broadstroke::reference::pastel_smear(cpu, pig, kW, kH, params);

    bool any_diff = false;
    for (std::size_t i = 0; i < gpu.size(); ++i) {
      if (std::fabs(gpu[i] - pig[i]) > 1e-4f) any_diff = true;
      if (std::fabs(gpu[i] - cpu[i]) > kTolerance) {
        std::fprintf(stderr, "FAIL idx=%zu gpu=%.6f cpu=%.6f\n", i,
                     (double)gpu[i], (double)cpu[i]);
        return 1;
      }
    }
    if (!any_diff) {
      std::fprintf(stderr, "FAIL: smear had no effect on the pigment field\n");
      return 1;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
