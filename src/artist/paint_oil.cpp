// Oil painting, worked the traditional way: a thin block-in with a large
// brush, then successively smaller brushes wherever the canvas still differs
// from the subject at that brush's scale. Each layer is left to set before
// the next, so later strokes sit on top instead of churning what is below;
// within a layer the paint is wet and strokes blend into their neighbors.
#include "artist/paths.hpp"
#include "artist/studio.hpp"
#include "media/bristle_brush.hpp"
#include "optics/mixer.hpp"

namespace bs {
namespace {

struct OilPass {
  const char* name;
  BrushSpec brush;
  float threshold;    // color difference (Oklab) that calls for a stroke
  float grid;         // spacing of candidate strokes, in brush widths
  float max_len;      // longest stroke, in brush widths
  float min_len;      // shortest stroke, in brush widths
  float press;        // typical effort
  float medium;       // share of thinner in the paint
  float coat_mm;      // film thickness the paint is mixed to leave
  int sweeps;         // times the artist steps back and corrects
  float detail_bias;  // how much more critical the artist is in detailed areas
};

HandParams hand_for(float width_mm) {
  HandParams h;
  h.cruise_mm_s = std::clamp(70.f + 9.f * width_mm, 70.f, 330.f);
  h.accel_mm_s2 = 9.f * h.cruise_mm_s;
  h.wander_mm = std::min(0.45f, 0.04f + 0.035f * width_mm);
  h.tremor_mm = 0.04f;
  h.lift_mm = std::min(7.f, 1.f + 0.6f * width_mm);
  h.curve_radius_mm = 6.f + 1.6f * width_mm;
  h.tilt = 0.65f;
  return h;
}

std::vector<OilPass> plan_passes(float long_mm, float effort) {
  auto flat = [](float w) {
    BrushSpec b;
    b.name = "flat"; b.shape = BrushSpec::Shape::Flat;
    b.width_mm = w; b.thick_mm = std::clamp(0.22f * w, 1.2f, 6.f);
    b.length_mm = std::clamp(1.1f * w, 7.f, 30.f);
    b.max_depth_mm = 0.22f * b.length_mm;
    b.stiffness = 1.f; b.spring = 0.6f; b.splay = 0.5f; b.clump = 0.55f;
    return b;
  };
  auto filbert = [&](float w) {
    BrushSpec b = flat(w);
    b.name = "filbert"; b.shape = BrushSpec::Shape::Filbert;
    b.length_mm = std::clamp(1.5f * w, 6.f, 26.f);
    b.max_depth_mm = 0.22f * b.length_mm;
    return b;
  };
  auto round = [](float w) {
    BrushSpec b;
    b.name = "round"; b.shape = BrushSpec::Shape::Round;
    b.width_mm = w; b.length_mm = std::clamp(4.5f * w, 5.f, 18.f);
    b.max_depth_mm = 0.2f * b.length_mm;
    b.stiffness = 0.55f; b.spring = 0.5f; b.splay = 0.9f; b.clump = 0.3f;
    return b;
  };
  float W = long_mm;
  std::vector<OilPass> p;
  //            name            brush               thr    grid  max   min  press med   coat  sweeps bias
  p.push_back({"block-in",      flat(W / 13.f),     0.030f, 0.55f, 7.f, 1.2f, 0.62f, 0.35f, 0.050f, 2, 0.0f});
  p.push_back({"large forms",   flat(W / 26.f),     0.050f, 0.60f, 7.f, 1.0f, 0.60f, 0.10f, 0.060f, 2, 0.6f});
  p.push_back({"modeling",      filbert(W / 52.f),  0.050f, 0.65f, 8.f, 1.0f, 0.58f, 0.05f, 0.060f, 2, 1.0f});
  if (effort >= 0.6f)
    p.push_back({"small forms", filbert(W / 100.f), 0.052f, 0.70f, 9.f, 1.0f, 0.55f, 0.05f, 0.055f, 2, 1.4f});
  if (effort >= 0.8f)
    p.push_back({"details",     round(W / 190.f),   0.056f, 0.75f, 10.f, 1.2f, 0.60f, 0.08f, 0.050f, 2, 1.8f});
  if (effort >= 1.0f)
    p.push_back({"accents",     round(W / 330.f),   0.062f, 0.80f, 12.f, 1.5f, 0.62f, 0.10f, 0.045f, effort >= 1.4f ? 2 : 1, 2.2f});
  return p;
}

struct Seed {
  Vec2 p;
  float err;
  float light;
};

void run_pass(Studio& st, Mixer& mixer, const OilPass& pass, int index, int count) {
  Canvas& cv = st.canvas;
  const float mm = cv.mm_per_px;
  const float wpx = pass.brush.width_mm / mm;
  float t0 = static_cast<float>(index) / count, t1 = static_cast<float>(index + 1) / count;
  st.begin_stage(std::string("oil: ") + pass.name, 0.02f + 0.96f * t0, 0.02f + 0.96f * t1);

  // The subject as seen at this brush's scale, and the way its forms run.
  Image ref = st.subject.color;
  gaussian_blur(ref, std::max(0.6f, 0.28f * wpx));
  FlowField flow = compute_flow(ref, std::max(2.f, 0.6f * wpx));
  guide_by_form(flow, st.subject.form);  // where the colors give no direction, the form does (spec 2.13)

  BristleBrush brush(pass.brush, mm, st.settings.seed * 131u + static_cast<uint32_t>(index));
  brush.body.coat_mm = pass.coat_mm;
  HandParams hand = hand_for(pass.brush.width_mm);

  const int strokes_before = st.strokes;
  const int block = std::max(2, static_cast<int>(wpx * 0.5f));
  const int g = std::max(block, static_cast<int>(pass.grid * wpx));
  Image ref_blocks = block_mean(ref, block);
  PigVec last_recipe{};
  bool have_load = false;

  for (int sweep = 0; sweep < pass.sweeps && !st.cancelled; ++sweep) {
    st.look();
    Image seen_blocks = block_mean(st.seen, block);
    const bool cover_all = index == 0 && sweep == 0;  // lay in the whole canvas first

    std::vector<Seed> seeds;
    for (int gy = 0; gy * g < cv.h; ++gy)
      for (int gx = 0; gx * g < cv.w; ++gx) {
        int bx0 = gx * g / block, by0 = gy * g / block;
        int bx1 = std::min(((gx + 1) * g + block - 1) / block, ref_blocks.w);
        int by1 = std::min(((gy + 1) * g + block - 1) / block, ref_blocks.h);
        float worst = -1.f, mean = 0.f;
        int n = 0, wx = bx0, wy = by0;
        for (int by = by0; by < by1; ++by)
          for (int bx = bx0; bx < bx1; ++bx) {
            float d = lab_dist(rgb_to_oklab(ref_blocks(bx, by)), rgb_to_oklab(seen_blocks(bx, by)));
            mean += d; ++n;
            if (d > worst) { worst = d; wx = bx; wy = by; }
          }
        if (n == 0) continue;
        mean /= n;
        Vec2 p{(wx + 0.5f) * block, (wy + 0.5f) * block};
        p.x = std::clamp(p.x + st.rng.range(-0.3f, 0.3f) * block, 1.f, cv.w - 2.f);
        p.y = std::clamp(p.y + st.rng.range(-0.3f, 0.3f) * block, 1.f, cv.h - 2.f);
        float detail = st.subject.detail(static_cast<int>(p.x), static_cast<int>(p.y));
        float thr = pass.threshold / (1.f + pass.detail_bias * detail);
        if (!cover_all && 0.5f * (mean + worst) < thr) continue;
        seeds.push_back({p, worst, st.subject.light(static_cast<int>(p.x), static_cast<int>(p.y))});
      }
    // Work dark to light with a good deal of wandering about the canvas.
    for (Seed& s : seeds) s.light += st.rng.range(-0.25f, 0.25f);
    std::sort(seeds.begin(), seeds.end(), [](const Seed& a, const Seed& b) { return a.light < b.light; });

    for (size_t si = 0; si < seeds.size() && !st.cancelled; ++si) {
      const Vec2 p = seeds[si].p;
      const float look_r = std::max(1.f, 0.25f * wpx);
      Rgb want = st.glance(ref, p, look_r);
      Lab want_lab = rgb_to_oklab(want);
      float detail = st.subject.detail(static_cast<int>(p.x), static_cast<int>(p.y));
      float thr = pass.threshold / (1.f + pass.detail_bias * detail);
      if (!cover_all) {
        // Look again: an earlier stroke may already have taken care of this spot.
        if (lab_dist(want_lab, rgb_to_oklab(st.glance(st.seen, p, look_r))) < thr) continue;
      }

      TraceParams tp;
      tp.step = std::max(2.f, 0.4f * wpx);
      tp.max_len = pass.max_len * wpx * st.rng.range(0.6f, 1.f);
      tp.min_len = pass.min_len * wpx;
      const float tolerance = cover_all ? 0.11f : 0.075f;
      auto keep_going = [&](Vec2 q) {
        Lab r = rgb_to_oklab(st.glance(ref, q, look_r));
        float d_stroke = lab_dist(r, want_lab);
        if (d_stroke > tolerance) return false;
        if (cover_all) return true;
        // Stop where the canvas is already closer to the subject than this color would be.
        float d_canvas = lab_dist(r, rgb_to_oklab(st.glance(st.seen, q, look_r)));
        return d_stroke < d_canvas;
      };
      std::vector<Vec2> path = smooth_path(trace_flow(flow, p, tp, keep_going, st.rng), 2);
      float len_mm = path_length(path) * mm;
      if (!cover_all) {
        // Is the stroke worth making? It must improve the canvas along its
        // whole length, not just at the spot that caught the eye.
        float gain = 0.f;
        for (const Vec2& q : path) {
          Lab r = rgb_to_oklab(st.glance(ref, q, look_r));
          gain += lab_dist(r, rgb_to_oklab(st.glance(st.seen, q, look_r))) - lab_dist(r, want_lab);
        }
        if (gain / path.size() < 0.4f * thr) continue;
      }

      // Mix the color; wipe the brush first when it is a different color.
      PigVec recipe = mixer.match_opaque(want);
      float change = 0.f;
      for (int i = 0; i < kMaxPig; ++i) change += std::abs(recipe[i] - last_recipe[i]);
      if (have_load && change > 0.25f) brush.wipe(0.06f);
      else if (have_load) brush.wipe(0.5f);
      float coat = pass.coat_mm * (0.8f + 0.5f * st.subject.light(static_cast<int>(p.x), static_cast<int>(p.y)));
      brush.body.coat_mm = coat;
      float volume = (len_mm + pass.brush.width_mm) * pass.brush.width_mm * coat * 1.25f;
      volume = std::max(volume, 0.8f * brush.tip_capacity_mm3());  // a dipped brush is wet to the tip
      brush.load(recipe, volume / (1.f - pass.medium), pass.medium);
      last_recipe = recipe;
      have_load = true;

      Gesture gesture;
      gesture.path = std::move(path);
      gesture.press = pass.press * st.rng.range(0.85f, 1.15f);
      brush.stroke(cv, perform(gesture, hand, mm, st.rng));
      ++st.strokes;
      st.look();
      st.report((sweep + static_cast<float>(si) / seeds.size()) / pass.sweeps);
    }
  }
  st.note(std::string(pass.name) + ": " + std::to_string(st.strokes - strokes_before) + " strokes, " +
          pass.brush.name + " " + std::to_string(static_cast<int>(pass.brush.width_mm * 10.f + 0.5f) / 10) + "." +
          std::to_string(static_cast<int>(pass.brush.width_mm * 10.f + 0.5f) % 10) + " mm");
  // Let the layer set before working over it.
  if (index + 1 < count) cv.dry(1.f - 0.5f * pass.medium);
}

}  // namespace

void paint_oil(Studio& st) {
  Mixer mixer(st.canvas.palette);
  float long_mm = std::max(st.canvas.w, st.canvas.h) * st.canvas.mm_per_px;
  std::vector<OilPass> passes = plan_passes(long_mm, st.settings.effort);
  for (size_t i = 0; i < passes.size() && !st.cancelled; ++i)
    run_pass(st, mixer, passes[i], static_cast<int>(i), static_cast<int>(passes.size()));
}

}  // namespace bs
