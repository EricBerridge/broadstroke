#include "apply_plan.hpp"

#include "broadstroke/tools/brush.hpp"
#include "broadstroke/tools/pastel.hpp"
#include "broadstroke/tools/pencil.hpp"

#include <stdexcept>
#include <unordered_map>

namespace broadstroke::host {

namespace {

// Tiny in-process preset cache. Loading the same brush from disk for
// every stroke would dominate runtime when the plan has thousands of
// steps; keying by name is enough since presets are immutable values.
template <typename T, typename Loader>
const T& cachedPreset(std::unordered_map<std::string, T>& cache,
                      const std::string& name, Loader load) {
  auto it = cache.find(name);
  if (it == cache.end()) it = cache.emplace(name, load(name)).first;
  return it->second;
}

}  // namespace

void applyPlan(Canvas& canvas, const planner::RenderPlan& plan,
               const ApplyOptions& opts) {
  // 1) Allocate layers in plan order. Indices in Step::layer_idx must
  //    match this allocation order, so the planner and executor must
  //    agree — this loop is the contract.
  std::vector<std::size_t> layer_idx;
  layer_idx.reserve(plan.layers.size());
  for (const auto& ls : plan.layers) {
    if (ls.kind == planner::LayerSpec::Kind::Graphite) {
      layer_idx.push_back(canvas.add_graphite_layer(ls.graphite_color));
    } else {
      layer_idx.push_back(
          canvas.add_pigment_layer(ls.K, ls.S, ls.thickness_scale));
    }
  }

  std::unordered_map<std::string, tools::PencilPreset> pencils;
  std::unordered_map<std::string, tools::BrushPreset>  brushes;
  std::unordered_map<std::string, tools::PastelPreset> pastels;

  // 2) Replay steps.
  const std::size_t total = plan.steps.size();
  for (std::size_t step_index = 0; step_index < total; ++step_index) {
    const auto& step = plan.steps[step_index];
    if (step.layer_idx >= layer_idx.size()) {
      throw std::runtime_error("applyPlan: step references out-of-range layer");
    }
    const std::size_t L = layer_idx[step.layer_idx];

    switch (step.op) {
      case planner::Step::Op::WatercolorStroke: {
        const auto& brush = cachedPreset(brushes, step.tool_name,
                                         tools::loadBrush);
        canvas.stroke_watercolor(L, step.stroke, brush);
        break;
      }
      case planner::Step::Op::PencilStroke: {
        const auto& pencil = cachedPreset(pencils, step.tool_name,
                                          tools::loadPencil);
        canvas.stroke_pencil(L, step.stroke, pencil);
        break;
      }
      case planner::Step::Op::PastelStroke: {
        const auto& pastel = cachedPreset(pastels, step.tool_name,
                                          tools::loadPastel);
        canvas.stroke_pastel(L, step.stroke, pastel);
        break;
      }
      case planner::Step::Op::PastelSmear: {
        const auto& pastel = cachedPreset(pastels, step.tool_name,
                                          tools::loadPastel);
        canvas.smear_pastel(L, step.ax, step.ay, step.bx, step.by, pastel);
        break;
      }
      case planner::Step::Op::Diffuse: {
        canvas.diffuse(L, step.diffuse_steps);
        break;
      }
    }

    if (opts.on_step) opts.on_step(canvas, step_index + 1, total);
  }
}

}  // namespace broadstroke::host
