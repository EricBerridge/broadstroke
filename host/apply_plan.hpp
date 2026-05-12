#pragma once

#include "broadstroke/planner/render_plan.hpp"

#include "canvas.hpp"

#include <cstddef>
#include <functional>

namespace broadstroke::host {

struct ApplyOptions {
  // Called after every step is dispatched, before the next one runs.
  // step_index is 1-based; total is plan.steps.size(). The callback
  // receives the canvas mutably so it can call canvas.compose() to grab
  // an intermediate snapshot — applyPlan otherwise never composes.
  //
  // Default (empty) callback runs nothing per step. Useful values:
  //   * progress logger that prints every K steps,
  //   * snapshot writer that composes + writeImage every K steps,
  //   * crash-tolerant streamer that writes the canvas state after every
  //     diffuse step so a long render's progress is preserved on disk.
  std::function<void(Canvas&, std::size_t, std::size_t)> on_step;
};

// Materialize a planner::RenderPlan onto a Canvas:
//   * For each LayerSpec, allocate the corresponding layer on the canvas.
//   * For each Step, load the named tool preset (with a small in-process
//     cache) and dispatch the matching Canvas method.
//   * After each step, fire ApplyOptions::on_step (if set).
//
// Throws std::runtime_error on missing preset files or impossible
// step/layer kind combinations.
void applyPlan(Canvas& canvas, const planner::RenderPlan& plan,
               const ApplyOptions& opts = {});

}  // namespace broadstroke::host
