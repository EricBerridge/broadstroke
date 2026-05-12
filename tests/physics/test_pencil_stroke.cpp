// GPU-vs-CPU equivalence test for compute/kernels/pencil_stroke.comp.
//
// Builds a small tooth field (constant), runs the same polyline stroke
// through both implementations, and asserts the resulting graphite
// densities agree within tolerance.

#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/reference/pencil_stroke.hpp"
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
    // A spatially-varying tooth field: a diagonal gradient. The
    // dependence of deposit on tooth lets us catch indexing bugs.
    std::vector<float> tooth(static_cast<std::size_t>(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
      for (std::uint32_t x = 0; x < kW; ++x) {
        const float u = static_cast<float>(x) / static_cast<float>(kW - 1);
        const float v = static_cast<float>(y) / static_cast<float>(kH - 1);
        tooth[static_cast<std::size_t>(y) * kW + x] = 0.3f + 0.6f * (u + v) * 0.5f;
      }
    }

    // A short stroke that crosses the field diagonally with varying
    // pressure. Choose coordinates that exercise both interior and
    // near-edge cases.
    broadstroke::tools::Stroke stroke;
    stroke.points = {
        {  8.0f,  12.0f, 0.30f},
        { 32.0f,  28.0f, 0.85f},
        { 60.0f,  20.0f, 0.55f},
        { 80.0f,  48.0f, 0.95f},
    };
    auto packed = broadstroke::tools::packStroke(stroke);

    broadstroke::reference::PencilStrokeParams params{
        /*tip_radius_px=*/2.5f,
        /*tip_softness=*/0.4f,
        /*deposit_strength=*/1.2f,
    };

    // GPU.
    broadstroke::compute::Context ctx;
    std::vector<float> g_zero(static_cast<std::size_t>(kW) * kH, 0.0f);
    auto graphite_g = ctx.upload(g_zero);
    auto tooth_g    = ctx.upload(tooth);
    auto stroke_g   = ctx.upload(packed);

    std::vector<float> packedParams = {
        static_cast<float>(kW),
        static_cast<float>(kH),
        static_cast<float>(stroke.points.size()),
        params.tip_radius_px,
        params.tip_softness,
        params.deposit_strength,
        0.0f, 0.0f,  // offset_x, offset_y (full-canvas dispatch)
    };
    auto params_g = ctx.upload(packedParams);

    const std::uint32_t gx = (kW + kLocalX - 1) / kLocalX;
    const std::uint32_t gy = (kH + kLocalY - 1) / kLocalY;
    ctx.dispatch("pencil_stroke",
                 {&graphite_g, &tooth_g, &stroke_g, &params_g},
                 {gx, gy, 1});
    auto gpu = graphite_g.download();

    // CPU.
    std::vector<float> cpu(static_cast<std::size_t>(kW) * kH, 0.0f);
    broadstroke::reference::pencil_stroke(
        cpu, tooth, packed, kW, kH, params);

    if (gpu.size() != cpu.size()) {
      std::fprintf(stderr, "FAIL: size mismatch gpu=%zu cpu=%zu\n",
                   gpu.size(), cpu.size());
      return 1;
    }
    bool any_nonzero = false;
    for (std::size_t i = 0; i < gpu.size(); ++i) {
      if (gpu[i] > 0.01f) any_nonzero = true;
      if (std::fabs(gpu[i] - cpu[i]) > kTolerance) {
        std::fprintf(stderr, "FAIL: index %zu (x=%zu, y=%zu) gpu=%.6f cpu=%.6f\n",
                     i, i % kW, i / kW,
                     static_cast<double>(gpu[i]),
                     static_cast<double>(cpu[i]));
        return 1;
      }
    }
    if (!any_nonzero) {
      // Defensive: catch a stroke that misses the canvas entirely.
      std::fprintf(stderr, "FAIL: stroke deposited nothing — bad test data?\n");
      return 1;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
