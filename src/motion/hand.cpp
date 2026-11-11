#include "motion/hand.hpp"

namespace bs {

std::vector<Pose> perform(const Gesture& g, const HandParams& hp, float mm_per_px, Rng& rng) {
  std::vector<Pose> out;
  if (g.path.size() < 2) return out;

  // --- Resample the intended path at a fixed arc-length step (in mm). ---
  const float step = 0.4f;
  std::vector<float> cum(g.path.size(), 0.f);
  for (size_t i = 1; i < g.path.size(); ++i)
    cum[i] = cum[i - 1] + length(g.path[i] - g.path[i - 1]) * mm_per_px;
  float total = cum.back();
  if (total < 1e-3f) return out;
  int n = std::max(2, static_cast<int>(std::ceil(total / step)) + 1);
  float h = total / (n - 1);
  std::vector<Vec2> pts(n);
  std::vector<float> eff(n);
  {
    size_t seg = 0;
    for (int k = 0; k < n; ++k) {
      float s = std::min(h * k, total);
      while (seg + 2 < g.path.size() && cum[seg + 1] < s) ++seg;
      float seg_len = std::max(cum[seg + 1] - cum[seg], 1e-6f);
      float t = clamp01((s - cum[seg]) / seg_len);
      pts[k] = lerp(g.path[seg], g.path[seg + 1], t);
      eff[k] = g.pressure.size() == g.path.size()
                   ? lerp(g.pressure[seg], g.pressure[seg + 1], t) : g.press;
    }
  }

  // --- Speed plan along the path. ---
  // Curvature limit (two-thirds power law: v ~ curvature^(-1/3)), then
  // forward/backward passes so the hand can actually reach and leave each
  // limit with its finite acceleration.
  float cruise = hp.cruise_mm_s * g.tempo;
  float vmin = 0.06f * cruise;
  std::vector<float> v(n, cruise);
  for (int k = 1; k + 1 < n; ++k) {
    Vec2 a = pts[k] - pts[k - 1], b = pts[k + 1] - pts[k];
    float la = length(a), lb = length(b);
    if (la < 1e-6f || lb < 1e-6f) continue;
    float cosang = std::clamp(dot(a, b) / (la * lb), -1.f, 1.f);
    float kappa = std::acos(cosang) / h;  // 1/mm
    float lim = std::pow(std::max(kappa * hp.curve_radius_mm, 1e-4f), -1.f / 3.f);
    v[k] = cruise * std::clamp(lim, 0.07f, 1.f);
  }
  if (g.land) v[0] = vmin;
  if (g.lift) v[n - 1] = std::max(vmin, 0.35f * v[n - 1]);  // strokes end in a flick, not a stop
  else v[n - 1] = vmin;
  float acc = hp.accel_mm_s2 * g.tempo;
  for (int k = 1; k < n; ++k) v[k] = std::min(v[k], std::sqrt(v[k - 1] * v[k - 1] + 2.f * acc * h));
  for (int k = n - 2; k >= 0; --k) v[k] = std::min(v[k], std::sqrt(v[k + 1] * v[k + 1] + 2.f * acc * h));

  // --- Integrate the hand tracking its aim point. ---
  const float dt = hp.dt;
  float omega = 2.f * kPi * hp.natural_hz;
  Vec2 tangent0 = normalize(pts[1] - pts[0]);
  Vec2 x = pts[0] * mm_per_px;  // work in mm
  Vec2 vel = g.land ? Vec2{} : tangent0 * v[0];
  Vec2 lean = hp.lean_follows ? tangent0 : normalize(hp.fixed_lean);
  Vec2 face = tangent0;
  // Tremor: a few sinusoids in the 8-12 Hz band with random phase.
  float tf[3], tp[3][2];
  for (int i = 0; i < 3; ++i) {
    tf[i] = rng.range(8.f, 12.f) * 2.f * kPi;
    tp[i][0] = rng.range(0.f, 2.f * kPi);
    tp[i][1] = rng.range(0.f, 2.f * kPi);
  }
  Vec2 wander{};  // Ornstein-Uhlenbeck aiming drift
  float pnoise = 0.f;
  const float wander_tau = 0.35f, pnoise_tau = 0.12f;
  float s = 0.f, t = 0.f;
  float lean_a = 1.f - std::exp(-dt * 2.f * kPi * hp.lean_hz);
  float face_a = 1.f - std::exp(-dt * 2.f * kPi * hp.face_hz);
  float lift_len = std::min(hp.lift_mm, 0.45f * total);
  float settle = 0.f;  // time spent after the aim point reached the end
  const int max_steps = 200000;
  for (int it = 0; it < max_steps; ++it) {
    // Advance the aim point.
    float fk = std::min(s / h, static_cast<float>(n - 1));
    int k = std::min(static_cast<int>(fk), n - 2);
    float fr = fk - k;
    float vs = std::max(lerp(v[k], v[k + 1], fr), vmin);
    bool at_end = s >= total;
    if (!at_end) s = std::min(s + vs * dt, total);
    else settle += dt;
    fk = std::min(s / h, static_cast<float>(n - 1));
    k = std::min(static_cast<int>(fk), n - 2);
    fr = fk - k;
    Vec2 aim = lerp(pts[k], pts[k + 1], fr) * mm_per_px;
    Vec2 dir = normalize(pts[k + 1] - pts[k]);
    if (at_end && g.lift) aim += dir * (vs * 0.3f * settle);  // follow-through while lifting

    wander += (wander * (-dt / wander_tau)) +
              Vec2{rng.normal(), rng.normal()} * (hp.wander_mm * std::sqrt(2.f * dt / wander_tau));
    Vec2 tremor{};
    for (int i = 0; i < 3; ++i) {
      tremor.x += std::sin(tf[i] * t + tp[i][0]);
      tremor.y += std::sin(tf[i] * t + tp[i][1]);
    }
    tremor *= hp.tremor_mm / 3.f;
    Vec2 target = aim + wander + tremor;

    // Damped second-order tracking (semi-implicit Euler).
    Vec2 a = (target - x) * (omega * omega) - vel * (2.f * hp.damping * omega);
    vel += a * dt;
    x += vel * dt;
    float speed = length(vel);

    // Tool orientation follows the travel direction with a lag.
    if (speed > 1.f) {
      Vec2 vd = vel / speed;
      if (hp.lean_follows) lean = normalize(lerp(lean, vd, lean_a), lean);
      face = normalize(lerp(face, vd, face_a), face);
    }

    // Effort: planned profile, loading on touchdown, unloading on lift-off,
    // slow fluctuation, and lighter touch at higher speed.
    pnoise += -pnoise * (dt / pnoise_tau) + rng.normal() * std::sqrt(2.f * dt / pnoise_tau);
    float e = lerp(eff[k], eff[k + 1], fr);
    e *= 1.f + hp.press_noise * pnoise;
    e *= 1.f - 0.18f * clamp01(speed / std::max(cruise, 1.f) - 0.5f);
    if (g.land) e *= smoothstep(0.f, hp.land_s, t + dt);
    if (g.lift) {
      float remain = total - s;
      float env = lift_len > 0.f ? smoothstep(0.f, lift_len, remain + 0.15f * lift_len) : 1.f;
      if (at_end) env *= clamp01(1.f - settle / 0.03f);
      e *= env;
    }
    e = clamp01(e);

    Pose p;
    p.pos = x / mm_per_px;
    p.press = e;
    p.tilt = hp.tilt;
    p.lean = lean;
    p.face = face;
    p.speed = speed;
    out.push_back(p);

    t += dt;
    if (at_end) {
      if (g.lift && settle >= 0.03f) break;
      if (!g.lift && (settle >= 0.08f || length(target - x) < 0.15f)) break;
    }
  }
  return out;
}

}  // namespace bs
