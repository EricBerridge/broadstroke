// Geometry of intended strokes: tracing along the subject's structure,
// smoothing, and the hatching patterns a drawing hand makes.
#pragma once

#include <functional>

#include "core/image.hpp"
#include "vision/analysis.hpp"

namespace bs {

float path_length(const std::vector<Vec2>& path);

// Corner-cutting subdivision (Chaikin); keeps the end points.
std::vector<Vec2> smooth_path(const std::vector<Vec2>& path, int iterations = 2);

struct TraceParams {
  float step = 4.f;         // px between path vertices
  float max_len = 200.f;    // px
  float min_len = 20.f;     // px traced before the stop test applies
  float inertia = 0.55f;    // 0 = follow the field exactly, 1 = go straight
  float default_angle = -0.6f;  // direction used where the subject has no structure
};

// Follows a flow field from `seed` in both directions. `keep_going(p)` is
// asked at each new vertex (beyond min_len) whether the stroke still belongs
// there; tracing stops on the first false or at the canvas edge.
std::vector<Vec2> trace_flow(const FlowField& flow, Vec2 seed, const TraceParams& tp,
                             const std::function<bool(Vec2)>& keep_going, Rng& rng);

// A back-and-forth shading scribble: `count` strokes of the given length and
// spacing, centered on `center`, laid along direction `dir` and advancing
// across it. Each stroke is a shallow arc, as a wrist pivoting makes.
std::vector<Vec2> zigzag_path(Vec2 center, Vec2 dir, float stroke_len, float spacing, int count,
                              float arc, Rng& rng);

// A single hatch stroke as a shallow arc.
std::vector<Vec2> arc_path(Vec2 from, Vec2 to, float bulge, int n = 10);

}  // namespace bs
