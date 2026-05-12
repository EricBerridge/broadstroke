// Structural tests for the additional planner styles (pencil_sketch and
// pastel_drawing) added in Stage 7. The watercolor planner already has
// its own test in test_planner.cpp.
//
// We synthesize a small Analysis with a known dark-half / light-half
// pattern and verify:
//   * pencil_sketch produces only PencilStroke steps,
//   * pastel_drawing produces a Pigment layer per stick and at least one
//     PastelStroke step,
//   * pressure values stay in the requested [min, max] range.

#include "broadstroke/analyzer/analyze.hpp"
#include "broadstroke/planner/plan.hpp"

#include <cstdio>
#include <vector>

namespace ba = broadstroke::analyzer;
namespace bp = broadstroke::planner;

namespace {

constexpr std::uint32_t kW = 64;
constexpr std::uint32_t kH = 48;

ba::Analysis makeDarkLightImage() {
  ba::Analysis a;
  a.width = kW; a.height = kH; a.num_bands = 4;
  const std::size_t n = static_cast<std::size_t>(kW) * kH;
  a.rgb.assign(n * 3, 0.0f);
  a.luminance.assign(n, 0.0f);
  a.edges.assign(n, 0.0f);
  a.flow.dx.assign(n, 1.0f);     // flow along +x
  a.flow.dy.assign(n, 0.0f);
  a.value_band.assign(n, 0);

  for (std::uint32_t y = 0; y < kH; ++y) {
    for (std::uint32_t x = 0; x < kW; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * kW + x;
      if (x < kW / 2) {
        a.rgb[i * 3 + 0] = 0.20f;
        a.rgb[i * 3 + 1] = 0.10f;
        a.rgb[i * 3 + 2] = 0.10f;
        a.luminance[i] = 0.15f;
        a.value_band[i] = 0;       // darkest band
      } else {
        a.rgb[i * 3 + 0] = 0.85f;
        a.rgb[i * 3 + 1] = 0.85f;
        a.rgb[i * 3 + 2] = 0.85f;
        a.luminance[i] = 0.85f;
        a.value_band[i] = 3;       // lightest band
      }
    }
  }
  return a;
}

}  // namespace

int main() {
  try {
    auto a = makeDarkLightImage();

    // ---- pencil_sketch ----
    {
      bp::PencilSketchOptions opt;
      opt.hatch_spacing = 6;
      auto plan = bp::plan_pencil_sketch(a, opt);

      if (plan.layers.size() != 1) {
        std::fprintf(stderr, "FAIL pencil: expected 1 layer, got %zu\n",
                     plan.layers.size());
        return 1;
      }
      if (plan.layers[0].kind != bp::LayerSpec::Kind::Graphite) {
        std::fprintf(stderr, "FAIL pencil: layer is not graphite\n");
        return 1;
      }
      if (plan.steps.empty()) {
        std::fprintf(stderr, "FAIL pencil: no steps emitted\n");
        return 1;
      }
      for (const auto& s : plan.steps) {
        if (s.op != bp::Step::Op::PencilStroke) {
          std::fprintf(stderr, "FAIL pencil: non-pencil step in plan\n");
          return 1;
        }
        for (const auto& pt : s.stroke.points) {
          if (pt.pressure < opt.min_pressure - 1e-5f ||
              pt.pressure > opt.max_pressure + 1e-5f) {
            std::fprintf(stderr, "FAIL pencil: pressure %.4f out of [%.4f, %.4f]\n",
                         (double)pt.pressure,
                         (double)opt.min_pressure,
                         (double)opt.max_pressure);
            return 1;
          }
        }
      }
    }

    // ---- pastel_drawing ----
    {
      std::vector<bp::Pigment> sticks = {
          {"pastel_sienna", {0.10f, 0.55f, 0.75f}, {0.55f, 0.55f, 0.50f},
                            {0.60f, 0.30f, 0.18f}},
          {"pastel_indigo", {0.85f, 0.55f, 0.20f}, {0.55f, 0.55f, 0.65f},
                            {0.20f, 0.20f, 0.50f}},
      };
      bp::PastelDrawingOptions opt;
      opt.seed_spacing = 6;
      opt.smear_strokes = true;
      auto plan = bp::plan_pastel_drawing(a, sticks, opt);

      if (plan.layers.size() != sticks.size()) {
        std::fprintf(stderr, "FAIL pastel: expected %zu layers, got %zu\n",
                     sticks.size(), plan.layers.size());
        return 1;
      }
      for (const auto& ls : plan.layers) {
        if (ls.kind != bp::LayerSpec::Kind::Pigment) {
          std::fprintf(stderr, "FAIL pastel: layer is not pigment\n");
          return 1;
        }
      }

      std::size_t pastel_strokes = 0;
      std::size_t smear_count    = 0;
      bool        any_diffuse    = false;
      for (const auto& s : plan.steps) {
        switch (s.op) {
          case bp::Step::Op::PastelStroke: ++pastel_strokes; break;
          case bp::Step::Op::PastelSmear:  ++smear_count;    break;
          case bp::Step::Op::Diffuse:      any_diffuse = true; break;
          default: break;
        }
      }
      if (pastel_strokes == 0) {
        std::fprintf(stderr, "FAIL pastel: no pastel strokes emitted\n");
        return 1;
      }
      if (any_diffuse) {
        std::fprintf(stderr, "FAIL pastel: pastel plan should not include diffusion\n");
        return 1;
      }
      // smear_strokes=true; with at least one stick that emits strokes,
      // we should have at least one smear step.
      if (smear_count == 0) {
        std::fprintf(stderr, "FAIL pastel: smear_strokes=true but zero smears emitted\n");
        return 1;
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
