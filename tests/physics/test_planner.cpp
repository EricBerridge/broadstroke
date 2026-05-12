// Sanity test for the watercolor planner. Builds a tiny synthetic Analysis
// (a left/right two-tone image plus a flow field), runs the planner with
// a small palette, and asserts:
//   * the plan has a layer per palette entry, plus the optional pencil layer;
//   * at least one watercolor stroke was emitted;
//   * Step::layer_idx values are in range;
//   * pressure values are in the requested [min, max] range.
//
// This is a structural test, not a visual test — the painted output is
// validated by the demo binary.

#include "broadstroke/analyzer/analyze.hpp"
#include "broadstroke/planner/plan.hpp"

#include <cstdio>
#include <vector>

namespace ba = broadstroke::analyzer;
namespace bp = broadstroke::planner;

namespace {

constexpr std::uint32_t kW = 64;
constexpr std::uint32_t kH = 48;

ba::Analysis makeAnalysis() {
  ba::Analysis a;
  a.width = kW; a.height = kH; a.num_bands = 4;
  const std::size_t n = static_cast<std::size_t>(kW) * kH;
  a.rgb.assign(n * 3, 0.0f);
  a.luminance.assign(n, 0.0f);
  a.edges.assign(n, 0.0f);
  a.flow.dx.assign(n, 0.0f);
  a.flow.dy.assign(n, 1.0f);    // flow points down everywhere
  a.value_band.assign(n, 0);

  for (std::uint32_t y = 0; y < kH; ++y) {
    for (std::uint32_t x = 0; x < kW; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * kW + x;
      // Left half: warm dark (sienna). Right half: cool light (substrate).
      if (x < kW / 2) {
        a.rgb[i * 3 + 0] = 0.55f;
        a.rgb[i * 3 + 1] = 0.30f;
        a.rgb[i * 3 + 2] = 0.20f;
        a.luminance[i] = 0.30f;
        a.value_band[i] = 1;
      } else {
        a.rgb[i * 3 + 0] = 0.85f;
        a.rgb[i * 3 + 1] = 0.85f;
        a.rgb[i * 3 + 2] = 0.85f;
        a.luminance[i] = 0.85f;
        a.value_band[i] = 3;
      }
    }
  }
  return a;
}

}  // namespace

int main() {
  try {
    auto a = makeAnalysis();

    std::vector<bp::Pigment> palette = {
        {"sienna",     {0.10f, 0.55f, 0.75f}, {0.30f, 0.30f, 0.32f}, {0.60f, 0.30f, 0.18f}},
        {"ultramarine",{0.85f, 0.45f, 0.05f}, {0.38f, 0.40f, 0.55f}, {0.18f, 0.22f, 0.55f}},
    };

    bp::PlanOptions opt;
    opt.seed_spacing = 8;
    opt.add_pencil_hatching = true;
    auto plan = bp::plan_watercolor(a, palette, opt);

    if (plan.layers.size() != palette.size() + 1) {
      std::fprintf(stderr, "FAIL: expected %zu layers, got %zu\n",
                   palette.size() + 1, plan.layers.size());
      return 1;
    }
    if (plan.steps.empty()) {
      std::fprintf(stderr, "FAIL: planner produced zero steps\n");
      return 1;
    }

    std::size_t watercolor_count = 0;
    std::size_t diffuse_count    = 0;
    std::size_t pencil_count     = 0;
    for (const auto& s : plan.steps) {
      if (s.layer_idx >= plan.layers.size()) {
        std::fprintf(stderr, "FAIL: step layer_idx %zu out of range\n", s.layer_idx);
        return 1;
      }
      switch (s.op) {
        case bp::Step::Op::WatercolorStroke:
          ++watercolor_count;
          for (const auto& pt : s.stroke.points) {
            if (pt.pressure < opt.min_pressure - 1e-5f ||
                pt.pressure > opt.max_pressure + 1e-5f) {
              std::fprintf(stderr, "FAIL: pressure %.4f out of [%.4f, %.4f]\n",
                           (double)pt.pressure,
                           (double)opt.min_pressure,
                           (double)opt.max_pressure);
              return 1;
            }
          }
          break;
        case bp::Step::Op::Diffuse:      ++diffuse_count;    break;
        case bp::Step::Op::PencilStroke: ++pencil_count;     break;
        default: break;
      }
    }

    if (watercolor_count == 0) {
      std::fprintf(stderr, "FAIL: no watercolor strokes emitted\n");
      return 1;
    }
    if (diffuse_count != palette.size()) {
      std::fprintf(stderr, "FAIL: expected one diffuse step per pigment "
                   "(=%zu), got %zu\n", palette.size(), diffuse_count);
      return 1;
    }
    // Pencil hatching only fires for value_band < hatch_max_band (default 1),
    // i.e., only band-0 pixels. Our test image has no band-0 pixels, so no
    // hatching strokes — that's fine. Just make sure the layer was allocated.
    (void)pencil_count;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
