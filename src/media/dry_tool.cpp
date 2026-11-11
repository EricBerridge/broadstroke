#include "media/dry_tool.hpp"

namespace bs {
namespace {

const float kNoTip = 1e6f;  // "no material here" in the tip height map

}  // namespace

DryTool::DryTool(const DrySpec& spec_in, float mm_per_px) : spec(spec_in), mm_per_px_(mm_per_px) {
  spec.lean = normalize(spec.lean, {0.7f, 0.7f});
  float s = sum(spec.recipe);
  if (s > 0.f)
    for (float& v : spec.recipe) v /= s;
  build_tip();
}

void DryTool::sharpen() {
  build_tip();
  worn_mm3_ = 0.f;
}

void DryTool::set_tilt(float tilt) {
  spec.tilt = tilt;
  sharpen();
}

HandParams DryTool::hand() const {
  HandParams h;
  h.lean_follows = false;  // the grip fixes how the tool leans
  h.fixed_lean = spec.lean;
  h.tilt = spec.tilt;
  h.cruise_mm_s = 140.f;
  h.accel_mm_s2 = 3200.f;
  h.curve_radius_mm = 9.f;
  h.natural_hz = 7.f;
  h.wander_mm = 0.22f;
  h.tremor_mm = 0.035f;
  h.land_s = 0.03f;
  h.lift_mm = 2.5f;
  h.press_noise = 0.1f;
  return h;
}

void DryTool::build_tip() {
  const float px = mm_per_px_;
  if (spec.tip == DrySpec::Tip::Cone) {
    n_ = 2 * static_cast<int>(std::ceil(spec.core_mm / px)) + 5;
    tip_ = Plane<float>(n_, n_, kNoTip);
    const float c = n_ * 0.5f;
    const float tau = std::clamp(spec.tilt, 0.f, 1.45f);
    const float alpha = std::clamp(spec.cone_deg, 3.f, 80.f) * kPi / 180.f;
    const float ax = std::sin(tau) * spec.lean.x, ay = std::sin(tau) * spec.lean.y, az = std::cos(tau);
    const float k = sqr(std::cos(alpha));
    const float max_reach = 0.5f * spec.core_mm / std::sin(alpha);  // slant length of the exposed cone
    for (int j = 0; j < n_; ++j)
      for (int i = 0; i < n_; ++i) {
        float x = (i + 0.5f - c) * px, y = (j + 0.5f - c) * px;
        float r2 = x * x + y * y;
        float q = x * ax + y * ay;
        float h = kNoTip;
        float A = az * az - k;
        if (std::abs(A) < 1e-6f) A = (A < 0.f ? -1e-6f : 1e-6f);
        float D = k * (q * q + A * r2);
        if (A > 0.f) {
          // Steep hold: the cone opens upward around the vertical; one sheet.
          float z = (-q * az + std::sqrt(std::max(D, 0.f))) / A;
          if (q + z * az >= 0.f) h = z;
        } else if (D >= 0.f) {
          // Leaning hold: the vertical line enters the cone at the lower root.
          float z = (q * az - std::sqrt(D)) / (-A);
          if (q + z * az >= 0.f) h = z;
        }
        if (h < kNoTip) {
          float dist = std::sqrt(r2 + h * h);
          if (dist > max_reach) h = kNoTip;  // beyond the core: wood or air
        }
        // Round the apex: a fresh point is a small sphere, not a singularity.
        if (h < kNoTip) h = std::sqrt(h * h + sqr(spec.point_mm)) - spec.point_mm;
        tip_(i, j) = h;
      }
    // The apex cell itself must exist even when the cone is narrower than a cell.
    int ci = static_cast<int>(c), cj = static_cast<int>(c);
    tip_(ci, cj) = std::min(tip_(ci, cj), 0.f);
  } else {
    float w = spec.core_mm, l = spec.bar_len_mm;
    n_ = static_cast<int>(std::ceil(std::sqrt(w * w + l * l) / px)) + 5;
    tip_ = Plane<float>(n_, n_, kNoTip);
    const float c = n_ * 0.5f;
    Vec2 along = spec.lean, across = perp(spec.lean);
    float r = std::max(spec.point_mm, 0.05f);
    // The stick is rocked slightly onto its leading edge.
    float rock = std::tan(std::clamp(1.5708f - spec.tilt, 0.f, 0.5f)) * 0.15f;
    for (int j = 0; j < n_; ++j)
      for (int i = 0; i < n_; ++i) {
        Vec2 p{(i + 0.5f - c) * px, (j + 0.5f - c) * px};
        float u = dot(p, across), v = dot(p, along);
        float du = 0.5f * w - std::abs(u), dv = 0.5f * l - std::abs(v);
        if (du < 0.f || dv < 0.f) continue;
        float e = std::max(0.f, r - std::min(du, dv));
        float h = r - std::sqrt(std::max(r * r - e * e, 0.f));
        h += rock * (v + 0.5f * l);
        tip_(i, j) = h;
      }
  }
  refresh_active();
  pen_ = 0.f;
}

void DryTool::refresh_active() {
  // Renormalize so the lowest point is at zero, then find the cells low
  // enough that they could ever reach the paper.
  float lo = kNoTip;
  for (float v : tip_.v) lo = std::min(lo, v);
  if (lo < kNoTip && lo != 0.f)
    for (float& v : tip_.v)
      if (v < kNoTip) v -= lo;
  const float cut = 0.6f;  // mm: generous bound on relief + indentation
  ax0_ = n_; ay0_ = n_; ax1_ = -1; ay1_ = -1;
  for (int j = 0; j < n_; ++j)
    for (int i = 0; i < n_; ++i)
      if (tip_(i, j) < cut) {
        ax0_ = std::min(ax0_, i); ay0_ = std::min(ay0_, j);
        ax1_ = std::max(ax1_, i); ay1_ = std::max(ay1_, j);
      }
  if (ax1_ < ax0_) { ax0_ = ay0_ = 0; ax1_ = ay1_ = n_ - 1; }
}

void DryTool::stroke(Canvas& canvas, const std::vector<Pose>& motion) {
  if (motion.empty()) return;
  refresh_active();
  Rect touched;
  // Sub-step in proportion to the contact patch: a broad stick does not need
  // half-cell steps to lay an even deposit.
  float extent = static_cast<float>(std::min(ax1_ - ax0_, ay1_ - ay0_) + 1);
  const float sub_px = std::clamp(0.12f * extent, 0.5f, 2.5f);
  Pose prev = motion.front();
  for (size_t k = 1; k < motion.size(); ++k) {
    const Pose& cur = motion[k];
    float dist = length(cur.pos - prev.pos);
    int sub = std::max(1, static_cast<int>(std::ceil(dist / sub_px)));
    for (int s = 1; s <= sub; ++s) {
      float t = static_cast<float>(s) / sub;
      float force = lerp(prev.press, cur.press, t) * spec.max_force;
      if (force > 1e-4f) step(canvas, lerp(prev.pos, cur.pos, t), force, dist / sub * mm_per_px_, touched);
    }
    travelled_mm_ += dist * mm_per_px_;
    prev = cur;
  }
  canvas.mark_dirty(touched.grown(1));
}

void DryTool::step(Canvas& canvas, Vec2 pos, float force, float ds_mm, Rect& touched) {
  const float c = n_ * 0.5f;
  const float cell_area = mm_per_px_ * mm_per_px_;
  // Canvas cells under the part of the tip that can touch.
  int x0 = static_cast<int>(std::floor(pos.x + ax0_ - c)) - 1;
  int x1 = static_cast<int>(std::ceil(pos.x + ax1_ + 1 - c)) + 1;
  int y0 = static_cast<int>(std::floor(pos.y + ay0_ - c)) - 1;
  int y1 = static_cast<int>(std::ceil(pos.y + ay1_ + 1 - c)) + 1;
  x0 = std::max(x0, 0); y0 = std::max(y0, 0);
  x1 = std::min(x1, canvas.w - 1); y1 = std::min(y1, canvas.h - 1);
  cells_.clear();
  float z_hi = -kNoTip;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      // Tip height above this cell (bilinear in the tip map).
      float tx = x + 0.5f - pos.x + c - 0.5f, ty = y + 0.5f - pos.y + c - 0.5f;
      int ix = static_cast<int>(std::floor(tx)), iy = static_cast<int>(std::floor(ty));
      if (ix < 0 || iy < 0 || ix + 1 >= n_ || iy + 1 >= n_) continue;
      float fx = tx - ix, fy = ty - iy;
      float a = tip_(ix, iy), b = tip_(ix + 1, iy), d = tip_(ix, iy + 1), e = tip_(ix + 1, iy + 1);
      // Interpolate over the corners that exist; a cell mostly off the tip does not touch.
      float wa = (1.f - fx) * (1.f - fy), wb = fx * (1.f - fy), wd = (1.f - fx) * fy, we = fx * fy;
      float wsum = 0.f, hsum = 0.f;
      if (a < kNoTip) { wsum += wa; hsum += wa * a; }
      if (b < kNoTip) { wsum += wb; hsum += wb * b; }
      if (d < kNoTip) { wsum += wd; hsum += wd * d; }
      if (e < kNoTip) { wsum += we; hsum += we * e; }
      if (wsum < 0.35f) continue;
      float th = hsum / wsum;
      float u = canvas.top(x, y) - th;
      cells_.push_back({x, y, u, tx, ty});
      z_hi = std::max(z_hi, u);
    }
  if (cells_.empty()) return;

  // Press the tool down until the contact pressure carries the hand's force:
  //   sum over cells of k * g(u - Z) * area = F.
  // g is a softened ramp: a cell is itself rough below the grid scale, so
  // contact comes on gradually as the tip approaches rather than all at once.
  const float ka = spec.stiffness * cell_area;
  const float sigma = 0.12f * std::max(canvas.surface.relief, 0.004f);
  auto ramp = [sigma](float d, float& slope) {
    float t = d / sigma;
    if (t > 8.f) { slope = 1.f; return d; }
    if (t < -8.f) { slope = 0.f; return 0.f; }
    float e = std::exp(t);
    slope = e / (1.f + e);
    return sigma * std::log1p(e);
  };
  float Z = z_hi - std::max(pen_, 1e-4f);
  for (int it = 0; it < 14; ++it) {
    float P = 0.f, dP = 0.f;
    for (const Cell& cl : cells_) {
      float slope;
      P += ka * ramp(cl.u - Z, slope);
      dP += ka * slope;
    }
    if (dP < 1e-9f) { Z = z_hi; continue; }
    float dZ = std::clamp((P - force) / dP, -0.05f, 0.05f);
    Z += dZ;
    if (std::abs(dZ) < 2e-5f) break;
  }
  pen_ = std::max(z_hi - Z, 0.f);

  const float grip = 0.35f + 0.65f * canvas.surface.grip;
  const float p_ref = std::max(spec.stiffness * std::max(canvas.surface.relief, 0.005f), 1e-3f);
  const float ds_cells = ds_mm / mm_per_px_;
  float carried_give = carried_vol_ > 0.f ? std::min(0.25f * ds_cells, 1.f) : 0.f;
  int n_active = 0;
  for (const Cell& cl : cells_)
    if (cl.u - Z > -2.f * sigma) ++n_active;
  if (n_active == 0) return;

  for (const Cell& cl : cells_) {
    if (cl.u - Z <= -2.f * sigma) continue;
    float slope_unused;
    float d = ramp(cl.u - Z, slope_unused);
    float p = spec.stiffness * d;  // N/mm^2
    float& cv = canvas.vol(cl.x, cl.y);
    PigVec& cp = canvas.pig(cl.x, cl.y);
    touched.include(cl.x, cl.y);
    float w = std::min(p / p_ref, 2.f) * ds_cells;  // how thoroughly this cell is worked

    // Existing material: lifted by an eraser, or dragged along by the tip.
    if (spec.lift > 0.f && cv > 0.f) {
      float f = std::min(spec.lift * w, 0.9f);
      for (float& v : cp) v *= 1.f - f;
      cv *= 1.f - f;
    }
    if (spec.smear > 0.f) {
      if (cv > 0.f) {
        float f = std::min(spec.smear * w, 0.5f);
        for (int i = 0; i < kMaxPig; ++i) {
          float t = cp[i] * f;
          cp[i] -= t;
          carried_pig_[i] += t;
        }
        carried_vol_ += cv * f;
        cv *= 1.f - f;
      }
      if (carried_give > 0.f) {
        float f = carried_give / n_active;
        for (int i = 0; i < kMaxPig; ++i) cp[i] += carried_pig_[i] * f;
        cv += carried_vol_ * f;
      }
    }

    // Burnishing: under pressure, material sitting on a crown is sheared off
    // into the lowest neighboring valley. Repeated heavy passes fill the
    // tooth until the surface is level and fully covered.
    if (spec.burnish > 0.f && cv > 0.f) {
      int bx = cl.x, by = cl.y;
      float here = canvas.top(cl.x, cl.y), low = here;
      const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto& o : nb) {
        int nx = cl.x + o[0], ny = cl.y + o[1];
        if (!canvas.vol.inside(nx, ny)) continue;
        float t = canvas.top(nx, ny);
        if (t < low) { low = t; bx = nx; by = ny; }
      }
      if (low < here) {
        float f = std::min(spec.burnish * w, 0.4f) * clamp01((here - low) / std::max(canvas.surface.relief, 0.004f));
        float moved = std::min(cv * f, 0.5f * (here - low));
        if (moved > 0.f) {
          float share = moved / cv;
          PigVec& np = canvas.pig(bx, by);
          for (int i = 0; i < kMaxPig; ++i) {
            float t = cp[i] * share;
            cp[i] -= t;
            np[i] += t;
          }
          cv -= moved;
          canvas.vol(bx, by) += moved;
          touched.include(bx, by);
        }
      }
    }

    // Abrasion: Archard wear, throttled once the surface is coated.
    if (spec.wear > 0.f && ds_mm > 0.f) {
      float adhesion = spec.adhesion_min + (1.f - spec.adhesion_min) * std::exp(-cv / spec.fill_mm);
      float dt = spec.wear * grip * p * ds_mm * adhesion;  // mm of film
      float keep = dt;
      if (spec.flow > 0.f) {
        // Soft material is squeezed off the crowns into the lowest neighbor.
        int bx = cl.x, by = cl.y;
        float low = canvas.top(cl.x, cl.y);
        const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& o : nb) {
          int nx = cl.x + o[0], ny = cl.y + o[1];
          if (!canvas.vol.inside(nx, ny)) continue;
          float t = canvas.top(nx, ny);
          if (t < low) { low = t; bx = nx; by = ny; }
        }
        if (bx != cl.x || by != cl.y) {
          float moved = dt * spec.flow;
          keep -= moved;
          canvas.vol(bx, by) += moved;
          PigVec& np = canvas.pig(bx, by);
          for (int i = 0; i < kMaxPig; ++i) np[i] += moved * spec.pigment_load * spec.recipe[i];
          touched.include(bx, by);
        }
      }
      cv += keep;
      for (int i = 0; i < kMaxPig; ++i) cp[i] += keep * spec.pigment_load * spec.recipe[i];
      deposited_mm3_ += dt * cell_area;
      // The same volume leaves the tip.
      int wi = std::clamp(static_cast<int>(std::floor(cl.tx + 0.5f)), 0, n_ - 1);
      int wj = std::clamp(static_cast<int>(std::floor(cl.ty + 0.5f)), 0, n_ - 1);
      if (tip_(wi, wj) < kNoTip) {
        tip_(wi, wj) += dt;
        worn_mm3_ += dt * cell_area;
      }
    }
  }
  if (carried_give > 0.f) {
    for (float& v : carried_pig_) v *= 1.f - carried_give;
    carried_vol_ *= 1.f - carried_give;
  }
}

// --- Ready-made tools ---------------------------------------------------

DrySpec pencil_spec(const std::string& grade) {
  DrySpec s;
  s.name = "pencil " + grade;
  s.tip = DrySpec::Tip::Cone;
  s.core_mm = 2.f; s.cone_deg = 11.f; s.point_mm = 0.10f; s.tilt = 0.9f;
  s.recipe[0] = 1.f;
  s.pigment_load = 1.f;
  s.flow = 0.05f; s.smear = 0.03f;
  s.max_force = 3.2f;
  // Clay-to-graphite ratio sets the grade: more clay is harder, wears less
  // and stops taking once a thinner film has formed.
  if (grade == "2h") { s.stiffness = 900.f; s.wear = 2.0e-4f; s.fill_mm = 0.0007f; s.adhesion_min = 0.03f; }
  else if (grade == "hb") { s.stiffness = 650.f; s.wear = 4.5e-4f; s.fill_mm = 0.0012f; s.adhesion_min = 0.05f; }
  else if (grade == "2b") { s.stiffness = 450.f; s.wear = 9.0e-4f; s.fill_mm = 0.0022f; s.adhesion_min = 0.07f; }
  else { s.name = "pencil 6b"; s.stiffness = 280.f; s.wear = 2.2e-3f; s.fill_mm = 0.0042f; s.adhesion_min = 0.10f; s.smear = 0.06f; }
  return s;
}

DrySpec charcoal_spec(bool compressed) {
  DrySpec s;
  s.recipe[0] = 1.f;
  s.pigment_load = 1.f;
  if (compressed) {
    s.name = "compressed charcoal";
    s.tip = DrySpec::Tip::Cone;
    s.core_mm = 4.f; s.cone_deg = 16.f; s.point_mm = 0.25f; s.tilt = 0.8f;
    s.stiffness = 120.f; s.wear = 3.0e-3f; s.fill_mm = 0.010f; s.adhesion_min = 0.12f;
    s.flow = 0.25f; s.burnish = 0.15f; s.smear = 0.10f; s.max_force = 3.f;
  } else {
    s.name = "vine charcoal";
    s.tip = DrySpec::Tip::Bar;
    s.core_mm = 5.f; s.bar_len_mm = 18.f; s.point_mm = 0.8f; s.tilt = 1.3f;
    s.stiffness = 0.6f; s.wear = 0.06f; s.fill_mm = 0.006f; s.adhesion_min = 0.06f;
    s.flow = 0.3f; s.smear = 0.18f; s.max_force = 2.2f;
  }
  return s;
}

DrySpec colored_pencil_spec(const PigVec& recipe) {
  DrySpec s;
  s.name = "colored pencil";
  s.tip = DrySpec::Tip::Cone;
  s.core_mm = 3.3f; s.cone_deg = 13.f; s.point_mm = 0.18f; s.tilt = 0.95f;
  s.stiffness = 300.f; s.wear = 2.2e-3f; s.fill_mm = 0.010f; s.adhesion_min = 0.06f;
  s.flow = 0.15f; s.burnish = 0.12f; s.smear = 0.04f; s.max_force = 4.f;
  s.pigment_load = 0.22f;  // pigment in wax and kaolin
  s.recipe = recipe;
  return s;
}

DrySpec crayon_spec(const PigVec& recipe) {
  DrySpec s;
  s.name = "wax crayon";
  s.tip = DrySpec::Tip::Cone;
  s.core_mm = 8.f; s.cone_deg = 32.f; s.point_mm = 0.7f; s.tilt = 0.75f;
  s.stiffness = 45.f; s.wear = 1.0e-2f; s.fill_mm = 0.024f; s.adhesion_min = 0.05f;
  s.flow = 0.3f; s.burnish = 0.35f; s.smear = 0.05f; s.max_force = 7.f;
  s.pigment_load = 0.10f;  // mostly paraffin: translucent, waxy
  s.recipe = recipe;
  return s;
}

DrySpec pastel_spec(const PigVec& recipe, bool side) {
  DrySpec s;
  s.name = side ? "soft pastel (side)" : "soft pastel (corner)";
  s.recipe = recipe;
  s.pigment_load = 0.55f;  // pigment with chalk and a trace of gum
  s.stiffness = 0.35f; s.wear = 0.2f; s.fill_mm = 0.030f; s.adhesion_min = 0.10f;
  s.flow = 0.35f; s.smear = 0.14f; s.max_force = 3.f;
  if (side) {
    s.tip = DrySpec::Tip::Bar;
    s.core_mm = 9.f; s.bar_len_mm = 16.f; s.point_mm = 1.2f; s.tilt = 1.4f;
  } else {
    s.tip = DrySpec::Tip::Cone;
    s.core_mm = 9.f; s.cone_deg = 40.f; s.point_mm = 0.8f; s.tilt = 0.8f;
    s.stiffness = 6.f; s.wear = 0.03f; s.max_force = 2.5f;
  }
  return s;
}

DrySpec pastel_pencil_spec(const PigVec& recipe) {
  DrySpec s;
  s.name = "pastel pencil";
  s.tip = DrySpec::Tip::Cone;
  s.core_mm = 4.f; s.cone_deg = 14.f; s.point_mm = 0.25f; s.tilt = 0.9f;
  s.stiffness = 60.f; s.wear = 7.0e-3f; s.fill_mm = 0.020f; s.adhesion_min = 0.10f;
  s.flow = 0.2f; s.smear = 0.06f; s.max_force = 3.f;
  s.pigment_load = 0.5f;
  s.recipe = recipe;
  return s;
}

DrySpec stump_spec(float width_mm) {
  DrySpec s;
  s.name = "stump";
  s.tip = DrySpec::Tip::Cone;
  s.core_mm = width_mm; s.cone_deg = 35.f; s.point_mm = 0.35f * width_mm; s.tilt = 0.8f;
  s.stiffness = 3.f; s.wear = 0.f; s.smear = 0.3f; s.max_force = 2.5f;
  return s;
}

DrySpec eraser_spec(float width_mm) {
  DrySpec s;
  s.name = "kneaded eraser";
  s.tip = DrySpec::Tip::Cone;
  s.core_mm = width_mm; s.cone_deg = 38.f; s.point_mm = 0.3f * width_mm; s.tilt = 0.7f;
  s.stiffness = 3.f; s.wear = 0.f; s.smear = 0.f; s.lift = 0.5f; s.max_force = 3.f;
  return s;
}

}  // namespace bs
