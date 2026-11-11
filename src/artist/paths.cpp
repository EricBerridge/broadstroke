#include "artist/paths.hpp"

namespace bs {

float path_length(const std::vector<Vec2>& path) {
  float l = 0.f;
  for (size_t i = 1; i < path.size(); ++i) l += length(path[i] - path[i - 1]);
  return l;
}

std::vector<Vec2> smooth_path(const std::vector<Vec2>& path, int iterations) {
  std::vector<Vec2> p = path;
  for (int it = 0; it < iterations && p.size() >= 3; ++it) {
    std::vector<Vec2> q;
    q.reserve(p.size() * 2);
    q.push_back(p.front());
    for (size_t i = 0; i + 1 < p.size(); ++i) {
      q.push_back(lerp(p[i], p[i + 1], 0.25f));
      q.push_back(lerp(p[i], p[i + 1], 0.75f));
    }
    q.push_back(p.back());
    p.swap(q);
  }
  return p;
}

std::vector<Vec2> trace_flow(const FlowField& flow, Vec2 seed, const TraceParams& tp,
                             const std::function<bool(Vec2)>& keep_going, Rng& rng) {
  const int w = flow.dir.w, h = flow.dir.h;
  auto field = [&](Vec2 p, Vec2 heading) {
    int x = std::clamp(static_cast<int>(p.x), 0, w - 1), y = std::clamp(static_cast<int>(p.y), 0, h - 1);
    Vec2 d = flow.dir(x, y);
    float c = flow.coherence(x, y);
    if (dot(d, heading) < 0.f) d = d * -1.f;
    // Where the subject has no clear direction the hand keeps its heading.
    float trust = smoothstep(0.08f, 0.45f, c) * smoothstep(0.04f, 0.3f, flow.strength(x, y)) * (1.f - tp.inertia);
    return normalize(lerp(heading, d, trust), heading);
  };
  int sx = std::clamp(static_cast<int>(seed.x), 0, w - 1), sy = std::clamp(static_cast<int>(seed.y), 0, h - 1);
  Vec2 start;
  if (flow.coherence(sx, sy) > 0.15f && flow.strength(sx, sy) > 0.08f) start = flow.dir(sx, sy);
  else {
    float ang = tp.default_angle + 0.5f * rng.normal();
    start = {std::cos(ang), std::sin(ang)};
  }
  // A hand prefers to pull strokes downward and to the right.
  if (start.y < -0.3f || (std::abs(start.y) <= 0.3f && start.x < 0.f)) start = start * -1.f;
  if (rng.uniform() < 0.15f) start = start * -1.f;

  auto walk = [&](Vec2 heading, float budget, std::vector<Vec2>& out) {
    Vec2 p = seed;
    float travelled = 0.f;
    while (travelled < budget) {
      heading = field(p, heading);
      Vec2 q = p + heading * tp.step;
      if (q.x < 1.f || q.y < 1.f || q.x > w - 2.f || q.y > h - 2.f) break;
      travelled += tp.step;
      if (travelled > tp.min_len * 0.5f && !keep_going(q)) break;
      out.push_back(q);
      p = q;
    }
  };
  std::vector<Vec2> fwd, back;
  walk(start, tp.max_len * 0.5f, fwd);
  walk(start * -1.f, tp.max_len * 0.5f, back);
  std::vector<Vec2> path;
  for (size_t i = back.size(); i-- > 0;) path.push_back(back[i]);
  path.push_back(seed);
  for (const Vec2& p : fwd) path.push_back(p);
  if (path.size() < 2) {
    // Nowhere to go along the field: a short dab in the default direction.
    path.clear();
    path.push_back(seed - start * (tp.min_len * 0.5f));
    path.push_back(seed + start * (tp.min_len * 0.5f));
  }
  return path;
}

std::vector<Vec2> arc_path(Vec2 from, Vec2 to, float bulge, int n) {
  std::vector<Vec2> p;
  Vec2 d = to - from;
  Vec2 nrm = perp(normalize(d));
  float len = length(d);
  for (int i = 0; i <= n; ++i) {
    float t = static_cast<float>(i) / n;
    p.push_back(lerp(from, to, t) + nrm * (bulge * len * 4.f * t * (1.f - t)));
  }
  return p;
}

std::vector<Vec2> zigzag_path(Vec2 center, Vec2 dir, float stroke_len, float spacing, int count,
                              float arc, Rng& rng) {
  // Turning points alternate between the two ends of the hatch while the
  // hand advances across it; each leg between them is one shallow arc.
  std::vector<Vec2> p;
  Vec2 d = normalize(dir), across = perp(d);
  float half = 0.5f * count * spacing;
  Vec2 prev;
  for (int k = 0; k <= count; ++k) {
    float off = -half + k * spacing + spacing * 0.25f * rng.normal();
    float reach = 0.5f * stroke_len * (1.f + 0.10f * rng.normal());
    Vec2 v = center + across * off + d * (k % 2 == 0 ? -reach : reach);
    if (k > 0) {
      std::vector<Vec2> seg = arc_path(prev, v, arc * (k % 2 == 0 ? -1.f : 1.f), 8);
      for (size_t i = (p.empty() ? 0 : 1); i < seg.size(); ++i) p.push_back(seg[i]);
    }
    prev = v;
  }
  return p;
}

}  // namespace bs
