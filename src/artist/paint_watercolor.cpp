// Watercolor, worked light to dark as the medium demands: the white of the
// paper is the only white, so every layer is a transparent wash that can only
// darken what is beneath it. Broad first washes are laid while their
// neighbors are still wet and run into each other; each later layer uses a
// smaller brush on dry paper, so its edges stay crisp. Before each wash the
// artist mixes the glaze that, over what is already on the paper, would come
// closest to the subject. The water does the rest: it flows, soaks in, dries
// from the edges and leaves the pigment where the simulation carries it.
#include "artist/paths.hpp"
#include "artist/studio.hpp"
#include "artist/target.hpp"
#include "media/bristle_brush.hpp"
#include "media/watercolor.hpp"
#include "optics/mixer.hpp"

namespace bs {
namespace {

struct WashPass {
  const char* name;
  float brush_mm;      // diameter of the round brush at the ferrule
  float patch_mm;      // area judged and washed at once
  float soften_mm;     // how broadly the subject is seen for this layer
  float threshold;     // color difference that calls for a wash
  float strength;      // share of the full correction applied (first washes stay light)
  int rounds;
  float seconds;       // working time per patch (lets earlier work dry)
};

BrushSpec round_brush(float w) {
  BrushSpec b;
  b.name = "round";
  b.shape = BrushSpec::Shape::Round;
  b.width_mm = w;
  b.length_mm = std::clamp(2.4f * w, 6.f, 32.f);
  b.max_depth_mm = 0.25f * b.length_mm;
  b.taper = 0.14f;
  b.stiffness = 0.3f; b.spring = 0.35f; b.splay = 1.4f; b.clump = 0.5f;
  return b;
}

class Watercolorist {
 public:
  explicit Watercolorist(Studio& st)
      : st_(st), cv_(st.canvas), mm_(st.canvas.mm_per_px), sim_(st.canvas), mixer_(st.canvas.palette) {
    cv_.overlay = &sim_.susp;  // wet pigment is part of what the artist sees
    // Tonal range: bare paper down to a full-strength neutral dark.
    ToneIntent intent;
    intent.paper = rgb_to_oklab(spectrum_to_rgb(cv_.surface.reflectance));
    intent.l_top = intent.paper.L;
    PigVec dark{};
    dark[4] = 1.4f;  // ultramarine
    dark[7] = 1.6f;  // burnt umber
    Spectrum kd, sd;
    cv_.palette.film(dark, kd, sd);
    intent.l_dark = rgb_to_oklab(spectrum_to_rgb(km_over(kd, sd, cv_.surface.reflectance))).L;
    intent.chroma = 1.08f;
    intent.key = 1.25f;
    intent.emphasis = 0.25f;
    target_ = make_target(st.subject, intent, mm_);
    flow_ = compute_flow(st.subject.color, std::max(2.f, 2.f / mm_));
    st.note("tonal range L " + std::to_string(intent.l_dark).substr(0, 4) + " to " +
            std::to_string(intent.l_top).substr(0, 4));
  }
  ~Watercolorist() { cv_.overlay = nullptr; }

  void run_pass(const WashPass& pass, float lo, float hi, uint32_t seed) {
    st_.begin_stage(std::string("watercolor: ") + pass.name, lo, hi);
    int before = st_.strokes;
    BristleBrush brush(round_brush(pass.brush_mm), mm_, seed);
    const float coat = std::clamp(0.05f + 0.006f * pass.brush_mm, 0.06f, 0.14f);
    brush.body.coat_mm = coat;
    brush.body.tip_hold_mm = 0.5f;
    brush.body.rate = 1.2f;
    brush.body.mix = 0.15f;
    brush.body.level = 0.f;
    brush.body.feed = 0.3f;
    brush.set_wet({&sim_.water, &sim_.susp});
    HandParams hand;
    hand.cruise_mm_s = std::clamp(50.f + 7.f * pass.brush_mm, 60.f, 170.f);
    hand.accel_mm_s2 = 9.f * hand.cruise_mm_s;
    hand.tilt = 0.5f;
    hand.wander_mm = std::min(0.4f, 0.05f + 0.03f * pass.brush_mm);
    hand.lift_mm = std::min(5.f, 1.f + 0.4f * pass.brush_mm);

    // The subject as it is seen for this layer.
    Image ref = target_;
    gaussian_blur(ref, std::max(0.5f, pass.soften_mm / mm_));
    const float patch = pass.patch_mm / mm_;
    const float line_mm = 0.36f * pass.brush_mm;  // width of the mark at working pressure
    const float spacing = 0.7f * line_mm / mm_;

    for (int round = 0; round < pass.rounds && !st_.cancelled; ++round) {
      st_.look();
      // Work down the sheet, as a wash is carried from the top.
      std::vector<Vec2> centers;
      float stepc = patch * 0.9f;
      for (float y = 0.5f * stepc; y < cv_.h + 0.4f * stepc; y += stepc)
        for (float x = 0.5f * stepc; x < cv_.w + 0.4f * stepc; x += stepc)
          centers.push_back({std::clamp(x + st_.rng.range(-0.15f, 0.15f) * patch, 1.f, cv_.w - 2.f),
                             std::clamp(y + st_.rng.range(-0.15f, 0.15f) * patch, 1.f, cv_.h - 2.f)});

      for (size_t ci = 0; ci < centers.size() && !st_.cancelled; ++ci) {
        Vec2 c = centers[ci];
        Lab want = rgb_to_oklab(st_.glance(ref, c, patch * 0.3f));
        Lab have = rgb_to_oklab(st_.glance(st_.seen, c, patch * 0.3f));
        float err = lab_dist(want, have);
        int cx = std::clamp(static_cast<int>(c.x), 0, cv_.w - 1), cy = std::clamp(static_cast<int>(c.y), 0, cv_.h - 1);
        float detail = st_.subject.detail(cx, cy);
        bool worked = false;
        if (err >= pass.threshold / (1.f + 0.8f * detail) && want.L < have.L + 0.02f) {
          // Mix the glaze for this spot.
          Spectrum base = mean_reflectance(c, patch * 0.3f);
          Lab aim{have.L + (want.L - have.L) * pass.strength, have.a + (want.a - have.a) * pass.strength,
                  have.b + (want.b - have.b) * pass.strength};
          PigVec amount = mixer_.match_glaze(base, oklab_to_rgb(aim), 3.2f);
          if (sum(amount) > 1e-4f) {
            Spectrum kd, sd;
            cv_.palette.film(amount, kd, sd);
            Lab pred = rgb_to_oklab(spectrum_to_rgb(km_over(kd, sd, base)));
            Lab base_lab = rgb_to_oklab(spectrum_to_rgb(base));
            Lab delta{pred.L - base_lab.L, pred.a - base_lab.a, pred.b - base_lab.b};
            Lab after{have.L + delta.L, have.a + delta.a, have.b + delta.b};
            if (err - lab_dist(after, want) >= std::max(0.12f * err, 0.006f)) {
              wash_patch(brush, hand, ref, c, patch, spacing, amount, coat, delta, pass);
              worked = true;
            }
          }
        }
        // Time passes whether or not this patch needed anything.
        sim_.advance(worked ? pass.seconds : 0.1f * pass.seconds);
        st_.look();
        st_.report((round + static_cast<float>(ci) / centers.size()) / pass.rounds);
      }
      // Let the layer dry before judging it again.
      sim_.dry();
      st_.look();
    }
    st_.note(std::string("watercolor: ") + pass.name + ": " + std::to_string(st_.strokes - before) +
             " strokes, round " + std::to_string(static_cast<int>(pass.brush_mm + 0.5f)) + " mm");
  }

 private:
  Spectrum mean_reflectance(Vec2 c, float r) const {
    static const float off[5][2] = {{0, 0}, {0.6f, 0.2f}, {-0.6f, -0.2f}, {0.2f, -0.6f}, {-0.2f, 0.6f}};
    Spectrum acc = spectrum_fill(0.f);
    for (auto& o : off) {
      int x = std::clamp(static_cast<int>(c.x + o[0] * r), 0, cv_.w - 1);
      int y = std::clamp(static_cast<int>(c.y + o[1] * r), 0, cv_.h - 1);
      Spectrum s = cv_.reflectance(x, y);
      for (int b = 0; b < kBands; ++b) acc[b] += s[b] * 0.2f;
    }
    return acc;
  }

  void wash_patch(BristleBrush& brush, const HandParams& hand, const Image& ref, Vec2 c, float patch,
                  float spacing, const PigVec& amount, float coat, Lab delta, const WashPass& pass) {
    // Strokes run along the forms where the subject has a direction,
    // otherwise level across the sheet like a flat wash.
    int cx = std::clamp(static_cast<int>(c.x), 0, cv_.w - 1), cy = std::clamp(static_cast<int>(c.y), 0, cv_.h - 1);
    Vec2 d{1.f, 0.f};
    if (flow_.coherence(cx, cy) > 0.25f && flow_.strength(cx, cy) > 0.12f) d = flow_.dir(cx, cy);
    d = rotate(d, st_.rng.normal() * 0.1f);
    if (d.x < 0.f) d = d * -1.f;
    Vec2 across = perp(d);
    if (across.y < 0.f) across = across * -1.f;  // carry the wash downward
    const float d2 = std::max(sqr(delta.L) + sqr(delta.a) + sqr(delta.b), 1e-8f);
    const float min_run = std::max(3.f, 0.6f * pass.brush_mm / mm_);
    const float sample = 2.f;
    PigVec per_mm{};
    for (int i = 0; i < kMaxPig; ++i) per_mm[i] = amount[i] / coat;

    struct Run { Vec2 a, b; float need; };
    std::vector<Run> runs;
    int n = std::max(1, static_cast<int>(std::ceil(patch * 1.0f / spacing)));
    for (int i = 0; i < n; ++i) {
      float off = (i - 0.5f * (n - 1)) * spacing + st_.rng.normal() * 0.1f * spacing;
      float half = 0.58f * patch * (1.f + 0.08f * st_.rng.normal());
      Vec2 a = c + across * off - d * half, b = c + across * off + d * half;
      if (i % 2 == 1) std::swap(a, b);
      Vec2 dir = normalize(b - a);
      int ns = std::max(2, static_cast<int>(length(b - a) / sample));
      int run_start = -1;
      float run_sum = 0.f;
      for (int k = 0; k <= ns; ++k) {
        float val = -1.f;
        if (k < ns) {
          Vec2 p = a + dir * (k * sample);
          if (p.x >= 0.f && p.y >= 0.f && p.x < cv_.w && p.y < cv_.h) {
            Lab t = rgb_to_oklab(st_.glance(ref, p, 1.5f));
            Lab s = rgb_to_oklab(st_.glance(st_.seen, p, 1.5f));
            val = ((t.L - s.L) * delta.L + (t.a - s.a) * delta.a + (t.b - s.b) * delta.b) / d2;
          }
        }
        if (val > 0.3f) {
          if (run_start < 0) { run_start = k; run_sum = 0.f; }
          run_sum += val;
        } else if (run_start >= 0) {
          if ((k - run_start) * sample >= min_run)
            runs.push_back({a + dir * (run_start * sample), a + dir * (k * sample), run_sum / (k - run_start)});
          run_start = -1;
        }
      }
    }
    if (runs.empty()) return;
    // One charge of the brush for the patch.
    float area_mm2 = 0.f;
    for (const Run& r : runs) area_mm2 += length(r.b - r.a) * mm_ * 0.42f * pass.brush_mm;
    brush.wipe(0.03f);
    brush.load_wet(per_mm, area_mm2 * coat * 1.5f + brush.tip_capacity_mm3());
    for (size_t i = 0; i < runs.size(); ++i) {
      Gesture g;
      float len = length(runs[i].b - runs[i].a);
      g.path = arc_path(runs[i].a, runs[i].b, 0.02f * (i % 2 ? -1.f : 1.f), std::max(4, static_cast<int>(len / 8.f)));
      g.press = clamp01(0.7f * std::clamp(runs[i].need, 0.6f, 1.2f) * st_.rng.range(0.92f, 1.08f));
      brush.stroke(cv_, perform(g, hand, mm_, st_.rng));
      sim_.touch(brush.last_touched());
      ++st_.strokes;
    }
  }

  Studio& st_;
  Canvas& cv_;
  float mm_;
  WatercolorSim sim_;
  Mixer mixer_;
  Image target_;
  FlowField flow_;
};

}  // namespace

void paint_watercolor(Studio& st) {
  Watercolorist w(st);
  float effort = st.settings.effort;
  float W = std::max(st.canvas.w, st.canvas.h) * st.canvas.mm_per_px;
  uint32_t seed = st.settings.seed * 977u;
  //                name               brush      patch      soften      thr   strength rounds sec
  w.run_pass({"first washes",  W / 16.f,  W / 13.f,  W / 70.f,  0.024f, 0.75f, 1, 3.0f}, 0.00f, 0.18f, seed + 1);
  w.run_pass({"second washes", W / 30.f,  W / 28.f,  W / 160.f, 0.030f, 0.90f, 1, 2.5f}, 0.18f, 0.40f, seed + 2);
  w.run_pass({"shapes",        W / 55.f,  W / 55.f,  W / 400.f, 0.040f, 1.00f, effort >= 1.f ? 2 : 1, 2.0f}, 0.40f, 0.68f, seed + 3);
  if (effort >= 0.7f)
    w.run_pass({"details",     W / 110.f, W / 110.f, 0.f,       0.045f, 1.00f, 2, 1.5f}, 0.68f, 0.86f, seed + 4);
  if (effort >= 1.f)
    w.run_pass({"fine details", W / 190.f, W / 170.f, 0.f,      0.050f, 1.00f, effort >= 1.3f ? 2 : 1, 1.2f}, 0.86f, 1.00f, seed + 5);
}

}  // namespace bs
