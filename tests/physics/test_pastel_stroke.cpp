// GPU-vs-CPU equivalence for pastel_stroke.

#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/reference/pastel_stroke.hpp"
#include "broadstroke/tools/stroke.hpp"

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
    std::vector<float> tooth(static_cast<std::size_t>(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
      for (std::uint32_t x = 0; x < kW; ++x) {
        const float u = static_cast<float>(x) / (kW - 1);
        const float v = static_cast<float>(y) / (kH - 1);
        tooth[static_cast<std::size_t>(y) * kW + x] = 0.25f + 0.65f * 0.5f * (u + v);
      }
    }

    broadstroke::tools::Stroke s;
    s.points = {
        { 12.0f,  10.0f, 0.40f},
        { 38.0f,  30.0f, 0.85f},
        { 70.0f,  22.0f, 0.65f},
        { 86.0f,  52.0f, 0.95f},
    };
    auto packed = broadstroke::tools::packStroke(s);

    broadstroke::reference::PastelStrokeParams params{
        /*tip_radius_px=*/5.0f,
        /*tip_softness=*/0.55f,
        /*deposit_strength=*/1.4f,
    };

    broadstroke::compute::Context ctx;
    std::vector<float> zero(static_cast<std::size_t>(kW) * kH, 0.0f);
    auto pigment_g = ctx.upload(zero);
    auto tooth_g   = ctx.upload(tooth);
    auto stroke_g  = ctx.upload(packed);
    std::vector<float> packedParams = {
        static_cast<float>(kW), static_cast<float>(kH),
        static_cast<float>(s.points.size()),
        params.tip_radius_px, params.tip_softness, params.deposit_strength,
        0.0f, 0.0f,
    };
    auto params_g = ctx.upload(packedParams);

    const std::uint32_t gx = (kW + kLocalX - 1) / kLocalX;
    const std::uint32_t gy = (kH + kLocalY - 1) / kLocalY;
    ctx.dispatch("pastel_stroke",
                 {&pigment_g, &tooth_g, &stroke_g, &params_g},
                 {gx, gy, 1});
    auto gpu = pigment_g.download();

    std::vector<float> cpu(static_cast<std::size_t>(kW) * kH, 0.0f);
    broadstroke::reference::pastel_stroke(cpu, tooth, packed, kW, kH, params);

    bool any_nonzero = false;
    for (std::size_t i = 0; i < gpu.size(); ++i) {
      if (gpu[i] > 0.01f) any_nonzero = true;
      if (std::fabs(gpu[i] - cpu[i]) > kTolerance) {
        std::fprintf(stderr, "FAIL idx=%zu gpu=%.6f cpu=%.6f\n", i,
                     (double)gpu[i], (double)cpu[i]);
        return 1;
      }
    }
    if (!any_nonzero) {
      std::fprintf(stderr, "FAIL: stroke deposited nothing\n");
      return 1;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
