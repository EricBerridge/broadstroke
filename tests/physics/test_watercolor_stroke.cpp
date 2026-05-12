// GPU-vs-CPU equivalence for watercolor_stroke. Spatially-varying tooth
// catches indexing bugs; the stroke is chosen to exercise both interior
// pixels and near-edge cases.

#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/reference/watercolor_stroke.hpp"
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
        tooth[static_cast<std::size_t>(y) * kW + x] = 0.3f + 0.6f * 0.5f * (u + v);
      }
    }

    broadstroke::tools::Stroke stroke;
    stroke.points = {
        {  6.0f,  10.0f, 0.30f},
        { 30.0f,  26.0f, 0.90f},
        { 58.0f,  18.0f, 0.55f},
        { 82.0f,  50.0f, 0.95f},
    };
    auto packed = broadstroke::tools::packStroke(stroke);

    broadstroke::reference::WatercolorStrokeParams params{
        /*tip_radius_px=*/4.0f,
        /*tip_softness=*/0.6f,
        /*water_load=*/0.7f,
        /*pigment_load=*/0.9f,
    };

    broadstroke::compute::Context ctx;
    std::vector<float> zero(static_cast<std::size_t>(kW) * kH, 0.0f);
    auto pigment_g = ctx.upload(zero);
    auto wetness_g = ctx.upload(zero);
    auto tooth_g   = ctx.upload(tooth);
    auto stroke_g  = ctx.upload(packed);

    std::vector<float> packedParams = {
        static_cast<float>(kW), static_cast<float>(kH),
        static_cast<float>(stroke.points.size()),
        params.tip_radius_px, params.tip_softness,
        params.water_load, params.pigment_load,
        0.0f, 0.0f,
    };
    auto params_g = ctx.upload(packedParams);

    const std::uint32_t gx = (kW + kLocalX - 1) / kLocalX;
    const std::uint32_t gy = (kH + kLocalY - 1) / kLocalY;
    ctx.dispatch("watercolor_stroke",
                 {&pigment_g, &wetness_g, &tooth_g, &stroke_g, &params_g},
                 {gx, gy, 1});
    auto gpu_p = pigment_g.download();
    auto gpu_w = wetness_g.download();

    std::vector<float> cpu_p(static_cast<std::size_t>(kW) * kH, 0.0f);
    std::vector<float> cpu_w(static_cast<std::size_t>(kW) * kH, 0.0f);
    broadstroke::reference::watercolor_stroke(
        cpu_p, cpu_w, tooth, packed, kW, kH, params);

    bool any_pigment = false;
    for (std::size_t i = 0; i < gpu_p.size(); ++i) {
      if (gpu_p[i] > 0.01f) any_pigment = true;
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
    if (!any_pigment) {
      std::fprintf(stderr, "FAIL: stroke deposited no pigment\n");
      return 1;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
