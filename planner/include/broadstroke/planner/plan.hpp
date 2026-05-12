#pragma once

#include "broadstroke/analyzer/analyze.hpp"
#include "broadstroke/planner/render_plan.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace broadstroke::planner {

// A pigment in the planner's working palette. Each carries its KM
// coefficients (which the planner writes into the produced LayerSpec)
// and a "swatch" — the visible body-color at full thickness — that the
// pigment-assignment step uses for nearest-color matching to source
// pixels.
struct Pigment {
  std::string name;                 // brush deposits this pigment; not a preset
  std::array<float, 3> K = {0.5f, 0.5f, 0.5f};
  std::array<float, 3> S = {0.5f, 0.5f, 0.5f};
  std::array<float, 3> swatch = {0.5f, 0.5f, 0.5f};
};

struct PlanOptions {
  // Streamline seed grid spacing. Smaller = denser washes / more strokes.
  std::uint32_t seed_spacing = 36;

  // Streamline integration. max_steps in each direction (forward +
  // backward from seed); step_size is integration step in pixels.
  int   max_steps = 28;
  float step_size = 1.6f;

  // Pressure modulation. The planner samples (1 − luminance) at each
  // stroke point as the "wash strength" then maps it into pressure.
  float pressure_scale = 1.2f;
  float min_pressure   = 0.10f;
  float max_pressure   = 0.95f;

  // KM thickness multiplier applied to the produced pigment layers.
  float thickness_scale = 2.0f;

  // Diffusion sweeps run after each pigment's strokes.
  std::uint32_t diffuse_steps_per_pigment = 12;

  // Brush preset name (must exist in resources/tools/<name>.json).
  std::string brush_name = "brush_round";

  // Skip seeds whose pigment-strength (color match × darkness) is below
  // this threshold. Higher = fewer strokes, only over strong matches.
  float strength_threshold = 0.12f;

  // Pencil hatching pass over the darkest value band.
  bool          add_pencil_hatching = true;
  std::string   pencil_name         = "pencil_4b";
  std::uint32_t hatch_spacing       = 14;
  float         hatch_length        = 18.0f;
  std::uint8_t  hatch_max_band      = 1;       // hatch bands [0, hatch_max_band-1]
};

// Build a RenderPlan from an Analysis given a working pigment palette.
//
//   1. Assign each pixel to the nearest pigment by RGB distance to the
//      pigment's swatch color. Per-pixel "wash strength" is the
//      darkness (1 − luminance) — darker pixels need more pigment.
//   2. For each pigment, walk a grid of seed points; if the seed is
//      assigned to this pigment and has strength above threshold, trace
//      a streamline through the flow field and emit a watercolor stroke.
//      Pressure is sampled from the strength field along the stroke.
//   3. After each pigment's strokes, append a diffusion step.
//   4. Optionally append a pencil hatching pass: short strokes
//      perpendicular to the flow field through pixels in the darkest
//      band(s).
//
// The returned RenderPlan is pure data: it has no GPU resources or live
// preset references. Host-side apply_plan() materializes it onto a Canvas.
RenderPlan plan_watercolor(const analyzer::Analysis& a,
                           const std::vector<Pigment>& palette,
                           const PlanOptions& opt = {});

// -------------------------------------------------------------------------
// Pencil sketch — pure linework, no pigment. Cross-contour hatching
// follows the flow field; an optional cross-hatch pass perpendicular to
// the first runs only over the darkest band(s).
// -------------------------------------------------------------------------
struct PencilSketchOptions {
  std::string   pencil_name      = "pencil_4b";
  std::array<float, 3> graphite_color = {0.06f, 0.05f, 0.05f};

  std::uint32_t hatch_spacing    = 10;
  float         hatch_length     = 18.0f;
  float         pressure_scale   = 1.1f;
  float         min_pressure     = 0.10f;
  float         max_pressure     = 0.95f;

  // Skip seeds whose darkness is below this threshold (lighter regions).
  float         strength_threshold = 0.18f;

  // Bands whose value < hatch_max_band get the primary hatch.
  // Bands whose value < cross_hatch_max_band get a perpendicular pass.
  std::uint8_t  hatch_max_band       = 3;   // hatch all but the lightest band
  std::uint8_t  cross_hatch_max_band = 1;   // cross-hatch only the darkest band
};

RenderPlan plan_pencil_sketch(const analyzer::Analysis& a,
                              const PencilSketchOptions& opt = {});

// -------------------------------------------------------------------------
// Pastel drawing — colored pigment via pastel sticks (no water, no
// diffusion). Same nearest-stick assignment as watercolor, but emits
// PastelStroke steps. Each pigment in the palette here uses Pigment::name
// as the *pastel preset name* (rather than the watercolor case where the
// pigment name is metadata and brush_name is shared).
// -------------------------------------------------------------------------
struct PastelDrawingOptions {
  std::uint32_t seed_spacing       = 28;
  int           max_steps          = 22;
  float         step_size          = 1.6f;
  float         pressure_scale     = 1.2f;
  float         min_pressure       = 0.10f;
  float         max_pressure       = 0.95f;
  float         thickness_scale    = 2.5f;
  float         strength_threshold = 0.12f;

  // Optional: emit a smear pass along each stick's longest stroke.
  bool          smear_strokes      = false;
};

RenderPlan plan_pastel_drawing(const analyzer::Analysis& a,
                               const std::vector<Pigment>& sticks,
                               const PastelDrawingOptions& opt = {});

}  // namespace broadstroke::planner
