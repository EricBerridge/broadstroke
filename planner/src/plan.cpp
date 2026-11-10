#include "broadstroke/planner/plan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace broadstroke::planner {

namespace {

inline std::size_t pix(std::uint32_t w, int x, int y) {
  return static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);
}

// Trace a streamline through the flow field starting at (sx, sy). The
// flow field is unsigned (eigenvectors don't pick a direction), so we
// disambiguate by always extending along whichever sign agrees with the
// previous step's direction. Stops on flow-zero, out-of-canvas, or
// max_steps reached.
tools::Stroke traceStreamline(const analyzer::Analysis& a,
                              float sx, float sy,
                              const PlanOptions& opt) {
  const float W = static_cast<float>(a.width);
  const float H = static_cast<float>(a.height);

  // Initial direction from the seed cell.
  const int ix0 = std::clamp(static_cast<int>(sx), 0, static_cast<int>(a.width)  - 1);
  const int iy0 = std::clamp(static_cast<int>(sy), 0, static_cast<int>(a.height) - 1);
  const std::size_t i0 = pix(a.width, ix0, iy0);
  const float dx0 = a.flow.dx[i0];
  const float dy0 = a.flow.dy[i0];

  // If the seed has zero flow, fall back to a simple horizontal segment.
  // Better than emitting an empty stroke (no-flow regions still want some
  // coverage; this is most common in solid-color areas).
  std::vector<tools::StrokePoint> forward, backward;
  forward.push_back({sx, sy, 0.5f});

  auto walk = [&](float x, float y, float prev_dx, float prev_dy,
                  std::vector<tools::StrokePoint>& out) {
    for (int k = 0; k < opt.max_steps; ++k) {
      const int ix = std::clamp(static_cast<int>(x), 0, static_cast<int>(a.width)  - 1);
      const int iy = std::clamp(static_cast<int>(y), 0, static_cast<int>(a.height) - 1);
      const std::size_t i = pix(a.width, ix, iy);
      float dx = a.flow.dx[i];
      float dy = a.flow.dy[i];
      if (dx * dx + dy * dy < 1e-4f) {
        // Fall through with a tiny step along the previous direction so
        // we cover flat regions instead of bailing immediately.
        dx = prev_dx;
        dy = prev_dy;
        if (dx * dx + dy * dy < 1e-4f) break;
      }
      // Bidirectional eigenvector → flip if it would U-turn.
      if (dx * prev_dx + dy * prev_dy < 0.0f) { dx = -dx; dy = -dy; }
      prev_dx = dx;
      prev_dy = dy;
      x += dx * opt.step_size;
      y += dy * opt.step_size;
      if (x < 0.0f || y < 0.0f || x >= W || y >= H) break;
      out.push_back({x, y, 0.5f});
    }
  };

  walk(sx, sy, dx0, dy0, forward);
  walk(sx, sy, -dx0, -dy0, backward);

  tools::Stroke s;
  s.points.reserve(backward.size() + forward.size());
  for (auto it = backward.rbegin(); it != backward.rend(); ++it) s.points.push_back(*it);
  for (auto& p : forward) s.points.push_back(p);
  return s;
}

// Walk a stroke and rewrite each point's pressure from the per-pixel
// strength field. Pressure scales with strength so the same brush gives
// a heavy stroke in dark regions and a barely-there one in light regions.
void modulatePressure(tools::Stroke& s,
                      const analyzer::Analysis& a,
                      const std::vector<float>& strength,
                      const PlanOptions& opt) {
  for (auto& pt : s.points) {
    const int ix = std::clamp(static_cast<int>(pt.x), 0, static_cast<int>(a.width)  - 1);
    const int iy = std::clamp(static_cast<int>(pt.y), 0, static_cast<int>(a.height) - 1);
    const float v = strength[pix(a.width, ix, iy)] * opt.pressure_scale;
    pt.pressure = std::clamp(v, opt.min_pressure, opt.max_pressure);
  }
}

inline float colorDist2(const float* a, const std::array<float, 3>& b) {
  const float dr = a[0] - b[0];
  const float dg = a[1] - b[1];
  const float db = a[2] - b[2];
  return dr * dr + dg * dg + db * db;
}

inline float perceptual_darkness(const analyzer::Analysis& a, std::size_t i) {
  const float value = std::pow(std::clamp(a.luminance[i], 0.0f, 1.0f), 1.0f / 2.2f);
  return 1.0f - value;
}

inline bool sky_like(const analyzer::Analysis& a, std::size_t i) {
  const float r = a.rgb[i * 3 + 0];
  const float g = a.rgb[i * 3 + 1];
  const float b = a.rgb[i * 3 + 2];
  return perceptual_darkness(a, i) < 0.48f && b > r + 0.08f && b > g + 0.025f;
}

tools::Stroke traceEdgeContour(const analyzer::Analysis& a,
                               float sx, float sy,
                               float pressure,
                               const PencilSketchOptions& opt) {
  const float W = static_cast<float>(a.width);
  const float H = static_cast<float>(a.height);
  const int ix0 = std::clamp(static_cast<int>(sx), 0, static_cast<int>(a.width) - 1);
  const int iy0 = std::clamp(static_cast<int>(sy), 0, static_cast<int>(a.height) - 1);
  const std::size_t i0 = pix(a.width, ix0, iy0);
  float dx0 = a.flow.dx[i0];
  float dy0 = a.flow.dy[i0];
  if (dx0 * dx0 + dy0 * dy0 < 0.01f) {
    dx0 = 0.7071f;
    dy0 = -0.7071f;
  }

  std::vector<tools::StrokePoint> forward;
  std::vector<tools::StrokePoint> backward;
  forward.push_back({sx, sy, pressure});

  auto walk = [&](float x, float y, float prev_dx, float prev_dy,
                  std::vector<tools::StrokePoint>& out) {
    for (int k = 0; k < opt.edge_max_steps; ++k) {
      const int ix = std::clamp(static_cast<int>(x), 0, static_cast<int>(a.width) - 1);
      const int iy = std::clamp(static_cast<int>(y), 0, static_cast<int>(a.height) - 1);
      const std::size_t i = pix(a.width, ix, iy);
      if (a.edges[i] < opt.edge_continue_threshold) break;

      float dx = a.flow.dx[i];
      float dy = a.flow.dy[i];
      if (dx * dx + dy * dy < 0.01f) {
        dx = prev_dx;
        dy = prev_dy;
      }
      if (dx * prev_dx + dy * prev_dy < 0.0f) {
        dx = -dx;
        dy = -dy;
      }

      x += dx * opt.edge_step_size;
      y += dy * opt.edge_step_size;
      if (x < 0.0f || y < 0.0f || x >= W || y >= H) break;
      const float local_pressure = std::clamp(
          a.edges[i] * opt.edge_pressure_scale + perceptual_darkness(a, i) * 0.15f,
          opt.min_pressure, opt.max_pressure);
      out.push_back({x, y, local_pressure});
      prev_dx = dx;
      prev_dy = dy;
    }
  };

  walk(sx, sy, dx0, dy0, forward);
  walk(sx, sy, -dx0, -dy0, backward);

  tools::Stroke s;
  s.points.reserve(backward.size() + forward.size());
  for (auto it = backward.rbegin(); it != backward.rend(); ++it) s.points.push_back(*it);
  for (const auto& p : forward) s.points.push_back(p);
  return s;
}

}  // namespace

RenderPlan plan_watercolor(const analyzer::Analysis& a,
                           const std::vector<Pigment>& palette,
                           const PlanOptions& opt) {
  RenderPlan plan;
  if (palette.empty()) return plan;

  // 1) One pigment layer per palette entry.
  std::vector<std::size_t> layer_for_pigment(palette.size());
  for (std::size_t p = 0; p < palette.size(); ++p) {
    LayerSpec ls;
    ls.kind = LayerSpec::Kind::Pigment;
    ls.K = palette[p].K;
    ls.S = palette[p].S;
    ls.thickness_scale = opt.thickness_scale;
    plan.layers.push_back(ls);
    layer_for_pigment[p] = plan.layers.size() - 1;
  }

  // 2) Nearest-pigment assignment + darkness-based strength field.
  const std::size_t n = static_cast<std::size_t>(a.width) * a.height;
  std::vector<std::uint8_t> assignment(n, 0);
  std::vector<float>        strength  (n, 0.0f);
  for (std::size_t i = 0; i < n; ++i) {
    float best = std::numeric_limits<float>::infinity();
    std::uint8_t best_p = 0;
    for (std::size_t p = 0; p < palette.size(); ++p) {
      const float d = colorDist2(&a.rgb[i * 3], palette[p].swatch);
      if (d < best) { best = d; best_p = static_cast<std::uint8_t>(p); }
    }
    assignment[i] = best_p;
    strength[i]   = std::clamp(1.0f - a.luminance[i], 0.0f, 1.0f);
  }

  // 3) Plan strokes per pigment.
  for (std::size_t p = 0; p < palette.size(); ++p) {
    for (std::uint32_t sy = opt.seed_spacing / 2; sy < a.height; sy += opt.seed_spacing) {
      for (std::uint32_t sx = opt.seed_spacing / 2; sx < a.width; sx += opt.seed_spacing) {
        const std::size_t i = pix(a.width, sx, sy);
        if (assignment[i] != p) continue;
        if (strength[i] < opt.strength_threshold) continue;

        tools::Stroke s = traceStreamline(a, static_cast<float>(sx) + 0.5f,
                                          static_cast<float>(sy) + 0.5f, opt);
        if (s.points.size() < 2) continue;
        modulatePressure(s, a, strength, opt);

        Step step;
        step.op        = Step::Op::WatercolorStroke;
        step.layer_idx = layer_for_pigment[p];
        step.stroke    = std::move(s);
        step.tool_name = opt.brush_name;
        plan.steps.push_back(std::move(step));
      }
    }

    Step diff;
    diff.op            = Step::Op::Diffuse;
    diff.layer_idx     = layer_for_pigment[p];
    diff.diffuse_steps = opt.diffuse_steps_per_pigment;
    plan.steps.push_back(std::move(diff));
  }

  // 4) Optional pencil hatching pass.
  if (opt.add_pencil_hatching) {
    LayerSpec ls;
    ls.kind = LayerSpec::Kind::Graphite;
    ls.graphite_color = {0.06f, 0.05f, 0.05f};
    plan.layers.push_back(ls);
    const std::size_t pencil_layer = plan.layers.size() - 1;

    for (std::uint32_t sy = opt.hatch_spacing / 2; sy < a.height; sy += opt.hatch_spacing) {
      for (std::uint32_t sx = opt.hatch_spacing / 2; sx < a.width; sx += opt.hatch_spacing) {
        const std::size_t i = pix(a.width, sx, sy);
        if (a.value_band[i] >= opt.hatch_max_band) continue;

        // Hatch perpendicular to flow (along gradient). For zero flow,
        // fall back to a fixed 45° angle so dark plain regions still get
        // hatching rather than nothing.
        float fx = a.flow.dx[i];
        float fy = a.flow.dy[i];
        float hx = -fy, hy = fx;
        if (hx * hx + hy * hy < 0.01f) { hx = 0.7071f; hy = 0.7071f; }

        const float L = opt.hatch_length * 0.5f;
        const float pressure = std::clamp(strength[i] * 1.1f, 0.3f, 0.95f);

        tools::Stroke s;
        s.points = {
            {static_cast<float>(sx) - L * hx,
             static_cast<float>(sy) - L * hy, pressure},
            {static_cast<float>(sx) + L * hx,
             static_cast<float>(sy) + L * hy, pressure}};

        Step step;
        step.op        = Step::Op::PencilStroke;
        step.layer_idx = pencil_layer;
        step.stroke    = std::move(s);
        step.tool_name = opt.pencil_name;
        plan.steps.push_back(std::move(step));
      }
    }
  }

  return plan;
}

// ---------------------------------------------------------------------------
// Pencil sketch
// ---------------------------------------------------------------------------

RenderPlan plan_pencil_sketch(const analyzer::Analysis& a,
                              const PencilSketchOptions& opt) {
  RenderPlan plan;

  LayerSpec ls;
  ls.kind = LayerSpec::Kind::Graphite;
  ls.graphite_color = opt.graphite_color;
  plan.layers.push_back(ls);
  const std::size_t L = plan.layers.size() - 1;

  // Helper: emit one short hatch stroke centered on (sx, sy) with the given
  // hatch direction (hx, hy). The opposite-end pressure is identical because
  // pencil hatching reads more naturally as a constant-pressure mark than
  // as a tapered one.
  auto emit_hatch = [&](std::uint32_t sx, std::uint32_t sy,
                        float hx, float hy, float pressure, float length,
                        const std::string& tool_name) {
    const float L_half = length * 0.5f;
    tools::Stroke s;
    s.points = {
        {static_cast<float>(sx) - L_half * hx,
         static_cast<float>(sy) - L_half * hy, pressure},
        {static_cast<float>(sx) + L_half * hx,
         static_cast<float>(sy) + L_half * hy, pressure}};
    Step step;
    step.op        = Step::Op::PencilStroke;
    step.layer_idx = L;
    step.stroke    = std::move(s);
    step.tool_name = tool_name;
    plan.steps.push_back(std::move(step));
  };

  auto emit_contour = [&](std::uint32_t sx, std::uint32_t sy, float pressure) {
    tools::Stroke stroke = traceEdgeContour(
        a, static_cast<float>(sx) + 0.5f, static_cast<float>(sy) + 0.5f,
        pressure, opt);
    if (stroke.points.size() < 2) return;
    Step step;
    step.op = Step::Op::PencilStroke;
    step.layer_idx = L;
    step.stroke = std::move(stroke);
    step.tool_name = opt.edge_pencil_name;
    plan.steps.push_back(std::move(step));
  };

  auto should_tone_hatch = [&](std::size_t i, float darkness) {
    if (sky_like(a, i)) return false;
    return darkness >= opt.primary_darkness_threshold ||
           a.edges[i] >= opt.edge_tone_threshold;
  };

  // Primary hatch: perpendicular to flow (along the local gradient).
  for (std::uint32_t sy = opt.hatch_spacing / 2; sy < a.height; sy += opt.hatch_spacing) {
    for (std::uint32_t sx = opt.hatch_spacing / 2; sx < a.width; sx += opt.hatch_spacing) {
      const std::size_t i = pix(a.width, sx, sy);
      const float strength = perceptual_darkness(a, i);
      if (strength < opt.strength_threshold) continue;
      if (!should_tone_hatch(i, strength)) continue;

      float fx = a.flow.dx[i];
      float fy = a.flow.dy[i];
      float hx = -fy, hy = fx;
      if (hx * hx + hy * hy < 0.01f) { hx = 0.7071f; hy = 0.7071f; }

      const float pressure = std::clamp(strength * opt.pressure_scale,
                                        opt.min_pressure, opt.max_pressure);
      emit_hatch(sx, sy, hx, hy, pressure, opt.hatch_length, opt.pencil_name);
    }
  }

  if (opt.add_edge_contours) {
    for (std::uint32_t sy = opt.edge_spacing / 2; sy < a.height; sy += opt.edge_spacing) {
      for (std::uint32_t sx = opt.edge_spacing / 2; sx < a.width; sx += opt.edge_spacing) {
        const std::size_t i = pix(a.width, sx, sy);
        const float edge = a.edges[i];
        if (edge < opt.edge_threshold) continue;
        if (sky_like(a, i)) continue;
        const float darkness = std::clamp(1.0f - a.luminance[i], 0.0f, 1.0f);
        const float pressure = std::clamp(edge * opt.edge_pressure_scale + darkness * 0.12f,
                                          opt.min_pressure, opt.max_pressure);
        emit_contour(sx, sy, pressure);
      }
    }
  }

  // Cross-hatch: perpendicular to the primary hatch (i.e., ALONG flow), only
  // over the darkest bands. Phase-shifted by half-spacing so the two grids
  // don't perfectly overlap.
  for (std::uint32_t sy = opt.hatch_spacing; sy < a.height; sy += opt.hatch_spacing) {
    for (std::uint32_t sx = opt.hatch_spacing; sx < a.width; sx += opt.hatch_spacing) {
      const std::size_t i = pix(a.width, sx, sy);
      const float strength = perceptual_darkness(a, i);
      if (strength < opt.cross_darkness_threshold) continue;

      float hx = a.flow.dx[i];
      float hy = a.flow.dy[i];
      if (hx * hx + hy * hy < 0.01f) { hx = 0.7071f; hy = -0.7071f; }

      const float pressure = std::clamp(strength * opt.pressure_scale,
                                        opt.min_pressure, opt.max_pressure);
      emit_hatch(sx, sy, hx, hy, pressure, opt.hatch_length, opt.pencil_name);
    }
  }

  return plan;
}

// ---------------------------------------------------------------------------
// Pastel drawing — uses the same nearest-pigment + streamline mechanism as
// watercolor, but writes PastelStroke steps and skips diffusion.
// ---------------------------------------------------------------------------

RenderPlan plan_pastel_drawing(const analyzer::Analysis& a,
                               const std::vector<Pigment>& sticks,
                               const PastelDrawingOptions& opt) {
  RenderPlan plan;
  if (sticks.empty()) return plan;

  std::vector<std::size_t> layer_for_stick(sticks.size());
  for (std::size_t p = 0; p < sticks.size(); ++p) {
    LayerSpec ls;
    ls.kind = LayerSpec::Kind::Pigment;
    ls.K = sticks[p].K;
    ls.S = sticks[p].S;
    ls.thickness_scale = opt.thickness_scale;
    plan.layers.push_back(ls);
    layer_for_stick[p] = plan.layers.size() - 1;
  }

  const std::size_t n = static_cast<std::size_t>(a.width) * a.height;
  std::vector<std::uint8_t> assignment(n, 0);
  std::vector<float>        strength  (n, 0.0f);
  for (std::size_t i = 0; i < n; ++i) {
    float best = std::numeric_limits<float>::infinity();
    std::uint8_t best_p = 0;
    for (std::size_t p = 0; p < sticks.size(); ++p) {
      const float d = colorDist2(&a.rgb[i * 3], sticks[p].swatch);
      if (d < best) { best = d; best_p = static_cast<std::uint8_t>(p); }
    }
    assignment[i] = best_p;
    strength[i]   = std::clamp(1.0f - a.luminance[i], 0.0f, 1.0f);
  }

  // Translate the (separate) PastelDrawingOptions into the streamline tracer
  // expected by traceStreamline + modulatePressure (which use PlanOptions).
  PlanOptions trace_opts;
  trace_opts.max_steps      = opt.max_steps;
  trace_opts.step_size      = opt.step_size;
  trace_opts.pressure_scale = opt.pressure_scale;
  trace_opts.min_pressure   = opt.min_pressure;
  trace_opts.max_pressure   = opt.max_pressure;

  for (std::size_t p = 0; p < sticks.size(); ++p) {
    // Track each stick's longest stroke for an optional smear pass.
    tools::Stroke longest;
    std::size_t   longest_len = 0;

    for (std::uint32_t sy = opt.seed_spacing / 2; sy < a.height; sy += opt.seed_spacing) {
      for (std::uint32_t sx = opt.seed_spacing / 2; sx < a.width; sx += opt.seed_spacing) {
        const std::size_t i = pix(a.width, sx, sy);
        if (assignment[i] != p) continue;
        if (strength[i] < opt.strength_threshold) continue;

        tools::Stroke s = traceStreamline(a, static_cast<float>(sx) + 0.5f,
                                          static_cast<float>(sy) + 0.5f, trace_opts);
        if (s.points.size() < 2) continue;
        modulatePressure(s, a, strength, trace_opts);

        if (s.points.size() > longest_len) {
          longest_len = s.points.size();
          longest = s;
        }

        Step step;
        step.op        = Step::Op::PastelStroke;
        step.layer_idx = layer_for_stick[p];
        step.stroke    = std::move(s);
        step.tool_name = sticks[p].name;        // pastel preset name
        plan.steps.push_back(std::move(step));
      }
    }

    if (opt.smear_strokes && longest_len >= 2) {
      Step smear;
      smear.op        = Step::Op::PastelSmear;
      smear.layer_idx = layer_for_stick[p];
      smear.tool_name = sticks[p].name;
      smear.ax = longest.points.front().x;
      smear.ay = longest.points.front().y;
      smear.bx = longest.points.back().x;
      smear.by = longest.points.back().y;
      plan.steps.push_back(std::move(smear));
    }
  }

  return plan;
}

}  // namespace broadstroke::planner
