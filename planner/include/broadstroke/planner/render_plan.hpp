#pragma once

#include "broadstroke/tools/stroke.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace broadstroke::planner {

// A layer the executor should add to the canvas before any steps run.
// Order in RenderPlan::layers determines the composite order (back to
// front); Step::layer_idx is an index into this vector.
struct LayerSpec {
  enum class Kind { Graphite, Pigment };
  Kind kind = Kind::Pigment;

  // Pigment-layer fields: KM coefficients per RGB channel, plus thickness.
  std::array<float, 3> K = {0.5f, 0.5f, 0.5f};
  std::array<float, 3> S = {0.5f, 0.5f, 0.5f};
  float thickness_scale  = 2.0f;

  // Graphite-layer field: the lit color of the graphite stroke.
  std::array<float, 3> graphite_color = {0.10f, 0.09f, 0.09f};
};

// A single instruction in the plan. The discriminator picks which named
// fields are meaningful. Keeping this as a flat struct (instead of a
// std::variant) lets the executor read it with a single switch and keeps
// the type printable for debugging.
struct Step {
  enum class Op { WatercolorStroke, PencilStroke, PastelStroke,
                  PastelSmear, Diffuse };
  Op op = Op::WatercolorStroke;
  std::size_t layer_idx = 0;

  // Stroke ops use these fields.
  tools::Stroke stroke;
  std::string   tool_name;   // brush / pencil / pastel preset name

  // PastelSmear uses these.
  float ax = 0, ay = 0, bx = 0, by = 0;

  // Diffuse uses this.
  std::uint32_t diffuse_steps = 0;
};

struct RenderPlan {
  std::vector<LayerSpec> layers;
  std::vector<Step>      steps;
};

}  // namespace broadstroke::planner
