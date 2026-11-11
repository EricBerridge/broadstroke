#include "media/bristle_brush.hpp"

namespace bs {
namespace {

const int kMaxSamples = 5;  // contact points simulated along a bent bristle

// Moves `amount` (mm of film) with composition from a carrier to a cell.
inline void move_film(float amount, float& src_vol, PigVec& src_pig, float& dst_vol, PigVec& dst_pig) {
  if (amount <= 0.f || src_vol <= 1e-9f) return;
  float f = std::min(amount / src_vol, 1.f);
  for (int i = 0; i < kMaxPig; ++i) {
    float d = src_pig[i] * f;
    src_pig[i] -= d;
    dst_pig[i] += d;
  }
  float dv = src_vol * f;
  src_vol -= dv;
  dst_vol += dv;
}

}  // namespace

BristleBrush::BristleBrush(const BrushSpec& spec, float mm_per_px, uint32_t seed)
    : spec_(spec), mm_per_px_(mm_per_px) {
  Rng rng(seed);
  float w_px = spec.width_mm / mm_per_px;
  const float pitch = 0.8f;  // lateral spacing of simulated bristles, px
  auto add = [&](float x_px, float y_px, float len) {
    Bristle b{};
    b.root = {x_px, y_px};
    b.len = len * (1.f + 0.012f * rng.normal());
    b.stiff = spec.stiffness * std::clamp(1.f + 0.22f * rng.normal(), 0.5f, 1.6f);
    b.dir = {0.f, -1.f};
    bristles_.push_back(b);
  };
  if (spec.shape == BrushSpec::Shape::Round) {
    float r = 0.5f * w_px;
    // A large round holds far more hairs than are worth following one by
    // one; beyond about 30 across, each simulated bristle stands for a lock.
    int n = std::clamp(static_cast<int>(std::ceil(w_px / pitch)), 1, 30);
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < n; ++i) {
        float x = (i + 0.5f + 0.6f * (rng.uniform() - 0.5f)) / n * 2.f * r - r;
        float y = (j + 0.5f + 0.6f * (rng.uniform() - 0.5f)) / n * 2.f * r - r;
        float q = std::sqrt(x * x + y * y) / std::max(r, 0.5f);
        if (q > 1.f) continue;
        // Pointed profile: the core hairs are the longest.
        add(x, y, spec.length_mm * (1.f - spec.taper * std::pow(q, 1.5f)));
      }
    if (bristles_.empty()) add(0.f, 0.f, spec.length_mm);
    tip_spread_x_ = tip_spread_y_ = 0.30f;  // a wet round comes to a point
  } else {
    float t_px = spec.thick_mm / mm_per_px;
    int cols = std::max(2, static_cast<int>(std::ceil(w_px / pitch)));
    int rows = std::clamp(static_cast<int>(std::round(t_px / 3.f)), 2, 5);
    for (int j = 0; j < rows; ++j)
      for (int i = 0; i < cols; ++i) {
        float x = ((i + 0.5f + 0.7f * (rng.uniform() - 0.5f)) / cols - 0.5f) * w_px;
        float y = ((j + 0.5f + 0.5f * (rng.uniform() - 0.5f)) / rows - 0.5f) * t_px;
        float len = spec.length_mm;
        if (spec.shape == BrushSpec::Shape::Filbert) {
          float q = std::abs(x) / (0.5f * w_px);
          len *= 1.f - 0.22f * q * q * q;  // rounded corners
        }
        add(x, y, len);
      }
    tip_spread_x_ = 1.f;
    tip_spread_y_ = 0.4f;  // the tip of a flat is a thin chisel edge
  }
  for (const Bristle& b : bristles_) max_len_ = std::max(max_len_, b.len);
  // Tufts: neighbors that gather together when the brush runs dry.
  float tuft_w = std::max(2.5f, 1.6f / mm_per_px);
  for (Bristle& b : bristles_) {
    float cell = std::floor(b.root.x / tuft_w);
    float jitter = hash01(static_cast<int>(cell), 17, seed) - 0.5f;
    b.tuft_x = (cell + 0.5f + 0.5f * jitter) * tuft_w;
  }
  cap_ = body.tip_hold_mm * 3.f;
}

void BristleBrush::load(const PigVec& frac, float volume_mm3, float medium) {
  cap_ = body.tip_hold_mm * 3.f;
  float cell_area = mm_per_px_ * mm_per_px_;
  float v = std::max(volume_mm3, 0.f) / cell_area;  // in cell-mm
  float pig_share = 1.f - clamp01(medium);
  float fs = std::max(sum(frac), 1e-9f);
  belly_vol_ += v;
  for (int i = 0; i < kMaxPig; ++i) belly_pig_[i] += v * pig_share * frac[i] / fs;
  wet_tips();
}

void BristleBrush::load_wet(const PigVec& pigment_per_mm, float volume_mm3) {
  cap_ = body.tip_hold_mm * 3.f;
  float v = std::max(volume_mm3, 0.f) / (mm_per_px_ * mm_per_px_);
  belly_vol_ += v;
  for (int i = 0; i < kMaxPig; ++i) belly_pig_[i] += v * pigment_per_mm[i];
  wet_tips();
}

void BristleBrush::wet_tips() {
  // Dipping wets every tip evenly, as far as the paint goes.
  float total = belly_vol_;
  for (const Bristle& b : bristles_) total += b.vol;
  float target = std::min(cap_, total / static_cast<float>(bristles_.size()));
  for (Bristle& b : bristles_) {
    float need = target - b.vol;
    if (need > 0.f) move_film(std::min(need, belly_vol_), belly_vol_, belly_pig_, b.vol, b.pig);
  }
}

float BristleBrush::tip_capacity_mm3() const {
  return body.tip_hold_mm * 3.f * static_cast<float>(bristles_.size()) * mm_per_px_ * mm_per_px_;
}

void BristleBrush::wipe(float keep) {
  keep = clamp01(keep);
  belly_vol_ *= keep;
  for (float& p : belly_pig_) p *= keep;
  for (Bristle& b : bristles_) {
    b.vol *= keep;
    for (float& p : b.pig) p *= keep;
  }
}

float BristleBrush::carried_mm3() const {
  float v = belly_vol_;
  for (const Bristle& b : bristles_) v += b.vol;
  return v * mm_per_px_ * mm_per_px_;
}

void BristleBrush::stroke(Canvas& canvas, const std::vector<Pose>& motion) {
  if (motion.empty()) return;
  for (Bristle& b : bristles_) {
    b.touching = false;
    b.prev_delta = 0.f;
  }
  Rect touched;
  Pose prev = motion.front();
  step(canvas, prev, 0.f, touched);
  for (size_t k = 1; k < motion.size(); ++k) {
    const Pose& cur = motion[k];
    float dist = length(cur.pos - prev.pos);
    int sub = std::max(1, static_cast<int>(std::ceil(dist / 0.5f)));
    for (int s = 1; s <= sub; ++s) {
      float t = static_cast<float>(s) / sub;
      Pose p;
      p.pos = lerp(prev.pos, cur.pos, t);
      p.press = lerp(prev.press, cur.press, t);
      p.tilt = lerp(prev.tilt, cur.tilt, t);
      p.lean = normalize(lerp(prev.lean, cur.lean, t), cur.lean);
      p.face = normalize(lerp(prev.face, cur.face, t), cur.face);
      p.speed = lerp(prev.speed, cur.speed, t);
      step(canvas, p, dist / sub, touched);
    }
    prev = cur;
  }
  last_touched_ = touched.grown(2).clipped(canvas.w, canvas.h);
  canvas.mark_dirty(last_touched_);
}

void BristleBrush::step(Canvas& canvas, const Pose& pose, float move_px, Rect& touched) {
  ++tick_;
  const float depth = pose.press * spec_.max_depth_mm;
  if (depth <= 0.f && move_px <= 0.f) return;
  const Vec2 across = perp(pose.face);
  const float sin_t = std::sin(pose.tilt), cos_t = std::cos(pose.tilt);
  const float relief = std::max(canvas.surface.relief, 0.015f);
  const float soft = 0.25f * relief + 0.003f;
  const float feed = 1.f - std::exp(-body.feed * move_px);
  const float inv_mm = 1.f / mm_per_px_;
  const Vec2 natural = pose.lean * -1.f;  // bristles trail behind the leaning handle

  for (size_t bi = 0; bi < bristles_.size(); ++bi) {
    Bristle& b = bristles_[bi];
    // Offset of this bristle in canvas axes; the leading side of a leaning
    // ferrule sits lower, so it is compressed more.
    Vec2 root_world = across * b.root.x + pose.face * b.root.y;
    float delta = depth - (max_len_ - b.len) + dot(root_world, pose.lean) * mm_per_px_ * sin_t;
    if (delta <= 0.f) {
      b.touching = false;
      b.prev_delta = 0.f;
      continue;
    }
    float squeeze = clamp01(delta / spec_.max_depth_mm);

    // Tips spread under compression and gather into tufts as the load drops.
    float fill = clamp01(b.vol / cap_);
    float sx = tip_spread_x_ + spec_.splay * 0.35f * squeeze;
    float sy = tip_spread_y_ + spec_.splay * 0.9f * squeeze;
    float tx = b.root.x * sx;
    float gather = spec_.clump * (1.f - fill) * 0.85f;
    tx = lerp(tx, b.tuft_x * sx, gather);
    Vec2 anchor = pose.pos + across * tx + pose.face * (b.root.y * sy);

    // Length of bristle bent onto the canvas (rigid-chord geometry).
    float z_root = b.len * cos_t - delta;
    float reach_mm = std::sqrt(std::max(b.len * b.len - z_root * z_root, 0.f)) - b.len * sin_t;
    float reach = std::max(reach_mm, 0.f) * inv_mm;  // px

    // The tip is held by friction and towed by the anchor; stiffness pulls it
    // back in line behind the handle.
    Vec2 old_tip = b.tip;
    if (!b.touching) {
      b.dir = natural;
      b.tip = anchor + b.dir * reach;
      old_tip = b.tip;
    } else {
      Vec2 towed = normalize(b.tip - anchor, b.dir);
      float w = 1.f - std::exp(-spec_.spring * b.stiff * move_px / (reach + 1.f));
      b.dir = normalize(lerp(towed, natural, w), towed);
      b.tip = anchor + b.dir * reach;
    }
    float slide = length(b.tip - old_tip);
    // Pressing down transfers paint even without sliding (a dab).
    float press_in = std::max(delta - b.prev_delta, 0.f) * inv_mm * 2.f;
    b.touching = true;
    b.prev_delta = delta;

    // Refill the tip from the belly, and let paint circulate between the
    // two: the belly is a large buffer that dilutes whatever the tip picks up.
    if (belly_vol_ > 1e-7f && feed > 0.f) {
      if (b.vol < cap_) {
        float want = (cap_ - b.vol) * feed;
        move_film(std::min(want, belly_vol_), belly_vol_, belly_pig_, b.vol, b.pig);
      }
      float circ = std::min(0.5f * feed * b.vol, 0.5f * belly_vol_);
      if (circ > 1e-7f) {
        float fb = circ / b.vol, fy = circ / belly_vol_;
        for (int i = 0; i < kMaxPig; ++i) {
          float from_b = b.pig[i] * fb, from_y = belly_pig_[i] * fy;
          b.pig[i] += from_y - from_b;
          belly_pig_[i] += from_b - from_y;
        }
      }
    }

    float p0 = b.stiff * delta / (0.5f * spec_.max_depth_mm);
    float exposure = (slide + press_in) * std::min(p0, 2.f);
    if (exposure <= 0.f) continue;
    float e_base = 1.f - std::exp(-body.rate * exposure);
    // Height at which the tip rides: on the peaks under light pressure,
    // down in the valleys under heavy pressure.
    float ride = relief * (1.f - 1.7f * clamp01(p0 * 0.8f));

    int ns = std::clamp(1 + static_cast<int>(reach / 1.5f), 1, kMaxSamples);
    for (int j = 0; j < ns; ++j) {
      Vec2 sp = b.tip - b.dir * (reach * 0.9f * j / ns);
      int cx = static_cast<int>(std::floor(sp.x)), cy = static_cast<int>(std::floor(sp.y));
      if (!canvas.vol.inside(cx, cy)) continue;
      // The film this brush works in: paint on the canvas, or water in a wash.
      const bool wet = wet_.water != nullptr;
      float& cv = wet ? (*wet_.water)(cx, cy) : canvas.vol(cx, cy);
      PigVec& cp = wet ? (*wet_.susp)(cx, cy) : canvas.pig(cx, cy);
      float tb = b.vol / ns;  // film on this part of the bristle
      // Does the bristle, or the paint it carries, reach this cell?
      float surface = canvas.tooth(cx, cy) + (wet ? cv : canvas.film_relief * cv);
      float reach_down = surface + body.bridge * tb - ride;
      float contact = smoothstep(-soft, soft, reach_down);
      if (contact <= 0.f) continue;
      float e = e_base * contact;
      touched.include(cx, cy);

      // Lubrication film the bristle would leave here.
      // Paint is pressed into the valleys of the weave and wiped thin over
      // its crowns, so a full coat levels the surface.
      float coat = body.coat_mm * std::pow(clamp01(b.vol / cap_), 0.7f) * (1.3f - 0.6f * clamp01(p0 * 0.6f));
      coat = std::max(0.25f * coat, coat - body.level * canvas.tooth(cx, cy));
      if (cv < coat) {
        float give = std::min(e * (coat - cv), 0.5f * b.vol);
        move_film(give, b.vol, b.pig, cv, cp);
      } else if (!wet) {
        // Scrape: part of the excess rides up into the bristle, part is
        // ploughed aside (a furrow with ridges) or pushed ahead (a bow wave).
        float excess = e * (cv - coat);
        float room = std::max(1.6f * cap_ - b.vol, 0.f);
        float up = std::min(excess * body.pickup, room);
        move_film(up, cv, cp, b.vol, b.pig);
        float push = std::min(excess * body.plough, cv);
        if (push > 0.f) {
          uint32_t hsh = static_cast<uint32_t>(bi) * 2654435761u + tick_ * 40503u + static_cast<uint32_t>(j);
          Vec2 to;
          if ((hsh >> 7) % 5u < 2u) to = sp - b.dir * 1.5f;  // ahead of the tip
          else to = sp + perp(b.dir) * (((hsh >> 3) & 1u) ? 1.2f : -1.2f);
          int nx = static_cast<int>(std::floor(to.x)), ny = static_cast<int>(std::floor(to.y));
          if (canvas.vol.inside(nx, ny) && (nx != cx || ny != cy)) {
            move_film(push, cv, cp, canvas.vol(nx, ny), canvas.pig(nx, ny));
            touched.include(nx, ny);
          }
        }
      }
      // Blending: the films in contact swap material both ways.
      float swap = e * body.mix * std::min(cv, b.vol / ns);
      if (swap > 1e-7f) {
        float fb = std::min(swap / std::max(b.vol, 1e-9f), 1.f);
        float fc = std::min(swap / std::max(cv, 1e-9f), 1.f);
        for (int i = 0; i < kMaxPig; ++i) {
          float from_b = b.pig[i] * fb, from_c = cp[i] * fc;
          b.pig[i] += from_c - from_b;
          cp[i] += from_b - from_c;
        }
        // Volumes are equal and opposite, so they are unchanged.
      }
    }
  }
}

}  // namespace bs
