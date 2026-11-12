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
  bool wet_in_wet = false;  // on skin, the place is dampened before the color is laid (spec 2.13)
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

// One wash to consider: a patch of the sheet, or with a scene the part of a
// patch that belongs to one item.
struct Unit {
  Vec2 c;        // center of the patch
  Vec2 at;       // where it is judged (inside the item)
  int region;    // item id, or -1 when there is no scene
  float share;   // share of the patch this unit covers
  int row, col;  // the patch's place in the grid
};

class Watercolorist {
 public:
  explicit Watercolorist(Studio& st)
      : st_(st), cv_(st.canvas), mm_(st.canvas.mm_per_px), sim_(st.canvas), mixer_(st.canvas.palette) {
    cv_.overlay = &sim_.susp;  // wet pigment is part of what the artist sees
    // Tonal range: bare paper down to a full-strength neutral dark.
    ToneIntent intent;
    intent.l_top = rgb_to_oklab(spectrum_to_rgb(cv_.surface.reflectance)).L;
    PigVec dark{};
    dark[4] = 1.4f;  // ultramarine
    dark[7] = 1.6f;  // burnt umber
    Spectrum kd, sd;
    cv_.palette.film(dark, kd, sd);
    intent.l_dark = rgb_to_oklab(spectrum_to_rgb(km_over(kd, sd, cv_.surface.reflectance))).L;
    intent.chroma = 1.08f;
    // Keyed a little below the subject's own tones. The washes come out
    // lighter than they are aimed (the paper shows through them), so an aim
    // at the subject's tones, or lighter, gave a pale picture with weak
    // darks and reds gone orange.
    intent.key = 0.85f;
    intent.emphasis = 0.25f;
    target_ = make_target(st.subject, intent, mm_);
    flow_ = compute_flow(st.subject.color, std::max(2.f, 2.f / mm_));
    if (!st.subject.item.empty()) {
      // Items whose edges are lost are worked as one, with no treatment of
      // their own.
      items_ = join_lost_items(st.subject.item, st.subject.lost);
      outlines_ = find_outlines(items_, st.subject.soft);
      for (int id = 0; id < 256; ++id) {
        skin_[id] = st.subject.treatment[id] == "skin" && !st.subject.lost[id];
        sky_[id] = (st.subject.treatment[id] == "sky" || st.subject.treatment[id] == "cloud") && !st.subject.lost[id];
        foliage_[id] = st.subject.treatment[id] == "foliage_mass" && !st.subject.lost[id];
        soft_[id] = st.subject.soft[id] != 0;
      }
    }
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

    // The subject as it is seen for this layer. With a scene each item is
    // seen on its own, without its neighbor's color mixed in at the outline.
    const bool by_item = !items_.empty();
    Image ref = target_;
    const float soften = std::max(0.5f, pass.soften_mm / mm_);
    if (by_item) ref = blur_within_items(ref, items_, soften);
    else gaussian_blur(ref, soften);
    const float patch = pass.patch_mm / mm_;
    const float line_mm = 0.36f * pass.brush_mm;  // width of the mark at working pressure
    const float spacing = 0.7f * line_mm / mm_;

    for (int round = 0; round < pass.rounds && !st_.cancelled; ++round) {
      st_.look();
      // Work down the sheet, as a wash is carried from the top. With a
      // scene, one item at a time: its washes join while they are wet, and
      // the dry edges fall on its outline instead of across it.
      std::vector<Unit> units;
      float stepc = patch * 0.9f;
      int row = 0;
      for (float y = 0.5f * stepc; y < cv_.h + 0.4f * stepc; y += stepc, ++row) {
        int col = 0;
        for (float x = 0.5f * stepc; x < cv_.w + 0.4f * stepc; x += stepc, ++col) {
          Vec2 c{std::clamp(x + st_.rng.range(-0.15f, 0.15f) * patch, 1.f, cv_.w - 2.f),
                 std::clamp(y + st_.rng.range(-0.15f, 0.15f) * patch, 1.f, cv_.h - 2.f)};
          if (by_item) add_units(units, c, stepc, row, col);
          else units.push_back({c, c, -1, 1.f, row, col});
        }
      }
      if (by_item) order_by_item(units);

      for (size_t ci = 0; ci < units.size() && !st_.cancelled; ++ci) {
        const Unit& u = units[ci];
        Vec2 c = u.at;
        Lab want = rgb_to_oklab(glance_in(ref, c, patch * 0.3f, u.region));
        Lab have = rgb_to_oklab(glance_in(st_.seen, c, patch * 0.3f, u.region));
        float err = lab_dist(want, have);
        int cx = std::clamp(static_cast<int>(c.x), 0, cv_.w - 1), cy = std::clamp(static_cast<int>(c.y), 0, cv_.h - 1);
        float detail = st_.subject.detail(cx, cy);
        bool worked = false;
        if (err >= pass.threshold / (1.f + 0.8f * detail) && want.L < have.L + 0.02f) {
          // Mix the glaze for this spot.
          Spectrum base = mean_reflectance(c, patch * 0.3f, u.region);
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
              if (by_item) {
                worked = wash_item(brush, hand, ref, u, patch, spacing, line_mm / mm_, amount, coat, delta, pass);
              } else {
                wash_patch(brush, hand, ref, c, patch, spacing, amount, coat, delta, pass);
                worked = true;
              }
            }
          }
        }
        // Time passes whether or not this patch needed anything.
        sim_.advance((worked ? pass.seconds : 0.1f * pass.seconds) * u.share);
        st_.look();
        st_.report((round + static_cast<float>(ci) / units.size()) / pass.rounds);
      }
      // Let the layer dry before judging it again.
      sim_.dry();
      st_.look();
    }
    st_.note(std::string("watercolor: ") + pass.name + ": " + std::to_string(st_.strokes - before) +
             " strokes, round " + std::to_string(static_cast<int>(pass.brush_mm + 0.5f)) + " mm");
  }

 private:
  // The items a patch covers. Each gets its own wash, judged at a point of
  // the patch that lies inside the item.
  void add_units(std::vector<Unit>& units, Vec2 c, float stepc, int row, int col) const {
    const Plane<uint8_t>& item = items_;
    constexpr int kSide = 5, kCount = kSide * kSide;
    Vec2 pts[kCount];
    int ids[kCount];
    struct Found { int id, n; Vec2 sum; };
    Found found[kCount];
    int nf = 0;
    for (int j = 0; j < kSide; ++j)
      for (int i = 0; i < kSide; ++i) {
        int k = j * kSide + i;
        pts[k] = {std::clamp(c.x + (i - 2) * 0.2f * stepc, 0.f, cv_.w - 1.f),
                  std::clamp(c.y + (j - 2) * 0.2f * stepc, 0.f, cv_.h - 1.f)};
        ids[k] = item(static_cast<int>(pts[k].x), static_cast<int>(pts[k].y));
        int f = 0;
        while (f < nf && found[f].id != ids[k]) ++f;
        if (f == nf) found[nf++] = {ids[k], 0, Vec2{0.f, 0.f}};
        ++found[f].n;
        found[f].sum = found[f].sum + pts[k];
      }
    for (int f = 0; f < nf; ++f) {
      if (found[f].n < 2) continue;  // a sliver: left to a pass with a smaller patch
      Vec2 mean = found[f].sum * (1.f / found[f].n);
      int best = -1;
      float best_d = 0.f;
      for (int k = 0; k < kCount; ++k) {
        if (ids[k] != found[f].id) continue;
        float d = length(pts[k] - mean);
        if (best < 0 || d < best_d) { best = k; best_d = d; }
      }
      units.push_back({c, pts[best], found[f].id, static_cast<float>(found[f].n) / kCount, row, col});
    }
  }

  // Items in the order they are first met going down the sheet; within an
  // item, row by row, turning at the end of each row so that the next wash
  // starts beside the last.
  static void order_by_item(std::vector<Unit>& units) {
    int rank[256];
    std::fill(std::begin(rank), std::end(rank), -1);
    int next = 0;
    for (const Unit& u : units)
      if (rank[u.region] < 0) rank[u.region] = next++;
    std::stable_sort(units.begin(), units.end(), [&](const Unit& a, const Unit& b) {
      if (rank[a.region] != rank[b.region]) return rank[a.region] < rank[b.region];
      if (a.row != b.row) return a.row < b.row;
      int ca = a.row % 2 ? -a.col : a.col, cb = b.row % 2 ? -b.col : b.col;
      return ca < cb;
    });
  }

  // The artist's glance at a spot, looking only at the unit's own item.
  Rgb glance_in(const Image& img, Vec2 p, float radius, int region) const {
    if (region < 0) return st_.glance(img, p, radius);
    static const float off[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                    {0.7f, 0.7f}, {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};
    const Plane<uint8_t>& item = items_;
    Rgb acc;
    int n = 0;
    for (auto& o : off) {
      int x = std::clamp(static_cast<int>(p.x + o[0] * radius), 0, img.w - 1);
      int y = std::clamp(static_cast<int>(p.y + o[1] * radius), 0, img.h - 1);
      if (item(x, y) != region) continue;
      acc += img(x, y);
      ++n;
    }
    if (n == 0) return st_.glance(img, p, 0.f);
    return acc * (1.f / n);
  }

  Spectrum mean_reflectance(Vec2 c, float r, int region) const {
    static const float off[5][2] = {{0, 0}, {0.6f, 0.2f}, {-0.6f, -0.2f}, {0.2f, -0.6f}, {-0.2f, 0.6f}};
    Spectrum acc = spectrum_fill(0.f);
    for (auto& o : off) {
      int x = std::clamp(static_cast<int>(c.x + o[0] * r), 0, cv_.w - 1);
      int y = std::clamp(static_cast<int>(c.y + o[1] * r), 0, cv_.h - 1);
      if (region >= 0 && items_(x, y) != region) {
        // Outside the item: look at its judged point instead.
        x = std::clamp(static_cast<int>(c.x), 0, cv_.w - 1);
        y = std::clamp(static_cast<int>(c.y), 0, cv_.h - 1);
      }
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

  // How far the outline near a cell turns a stroke of this width: fully
  // within about one width of it, not at all beyond two.
  float outline_pull(int x, int y, float line_px) const {
    return 1.f - smoothstep(0.75f * line_px, 2.f * line_px, outlines_.dist(x, y));
  }

  // The path the hand is given for one run: its points thinned to about one
  // every 8 px, with the slight bow of a wrist stroke.
  static std::vector<Vec2> run_path(const std::vector<Vec2>& pts, float bulge) {
    std::vector<Vec2> p;
    size_t stride = std::clamp<size_t>(pts.size() / 4, 1, 4);
    for (size_t i = 0; i + 1 < pts.size(); i += stride) p.push_back(pts[i]);
    p.push_back(pts.back());
    Vec2 chord = p.back() - p.front();
    float len = length(chord);
    if (len > 1e-3f && p.size() > 2) {
      Vec2 nrm = perp(chord * (1.f / len));
      for (size_t i = 0; i < p.size(); ++i) {
        float t = static_cast<float>(i) / (p.size() - 1);
        p[i] = p[i] + nrm * (bulge * len * 4.f * t * (1.f - t));
      }
    }
    return p;
  }

  // A wash of one item within a patch (with a scene). As wash_patch, but
  // the strokes belong to the item: near its outline they run along it, and
  // they end short of it by part of the width of the mark, so the brush's
  // edge and not its middle meets the outline. Returns whether anything was
  // laid.
  bool wash_item(BristleBrush& brush, const HandParams& hand, const Image& ref, const Unit& u, float patch,
                 float spacing, float line_px, const PigVec& amount, float coat, Lab delta, const WashPass& pass) {
    const Plane<uint8_t>& item = items_;
    auto cell_x = [&](Vec2 p) { return std::clamp(static_cast<int>(p.x), 0, cv_.w - 1); };
    auto cell_y = [&](Vec2 p) { return std::clamp(static_cast<int>(p.y), 0, cv_.h - 1); };
    int ax = cell_x(u.at), ay = cell_y(u.at);
    Vec2 d{1.f, 0.f};
    if (flow_.coherence(ax, ay) > 0.25f && flow_.strength(ax, ay) > 0.12f) d = flow_.dir(ax, ay);
    if (foliage_[u.region]) {
      // Foliage is clusters of leaves that face every way: each wash is
      // laid in a direction of its own.
      float turn = st_.rng.range(0.f, 3.14159265f);
      d = Vec2{std::cos(turn), std::sin(turn)};
    }
    float pull = outline_pull(ax, ay, line_px);
    if (pull > 0.f) {
      Vec2 t = outlines_.along(ax, ay);
      if (dot(t, d) < 0.f) t = t * -1.f;
      d = normalize(lerp(d, t, pull), d);
    }
    d = rotate(d, st_.rng.normal() * 0.1f);
    if (d.x < 0.f) d = d * -1.f;
    Vec2 across = perp(d);
    if (across.y < 0.f) across = across * -1.f;  // carry the wash downward
    const float d2 = std::max(sqr(delta.L) + sqr(delta.a) + sqr(delta.b), 1e-8f);
    const float min_run = std::max(3.f, 0.6f * pass.brush_mm / mm_);
    const float sample = 2.f;
    const float margin = 0.3f * line_px;  // kept from a hard outline
    PigVec per_mm{};
    for (int i = 0; i < kMaxPig; ++i) per_mm[i] = amount[i] / coat;

    // A stroke keeps the patch's direction, except that an outline it is
    // already running beside turns it along itself. An outline met head-on
    // does not turn the stroke; it ends it.
    auto steer = [&](Vec2 p, Vec2 straight, Vec2 heading) {
      int x = cell_x(p), y = cell_y(p);
      float pl = outline_pull(x, y, line_px);
      if (pl <= 0.f) return straight;
      Vec2 t = outlines_.along(x, y);
      float beside = dot(t, heading);
      if (beside < 0.f) { t = t * -1.f; beside = -beside; }
      return normalize(lerp(straight, t, pl * smoothstep(0.3f, 0.7f, beside)), heading);
    };
    auto need_at = [&](Vec2 p) {
      if (!(p.x >= 0.f && p.y >= 0.f && p.x < cv_.w && p.y < cv_.h)) return -1.f;
      int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
      if (item(x, y) != u.region || outlines_.dist(x, y) < margin || outlines_.soft(x, y) < 0.5f * margin) return -1.f;
      Lab t = rgb_to_oklab(st_.glance(ref, p, 1.5f));
      Lab s = rgb_to_oklab(st_.glance(st_.seen, p, 1.5f));
      return ((t.L - s.L) * delta.L + (t.a - s.a) * delta.a + (t.b - s.b) * delta.b) / d2;
    };

    struct Run { std::vector<Vec2> pts; float need; };
    std::vector<Run> runs;
    std::vector<Vec2> back, pts;
    int n = std::max(1, static_cast<int>(std::ceil(patch * 1.0f / spacing)));
    for (int i = 0; i < n; ++i) {
      float off = (i - 0.5f * (n - 1)) * spacing + st_.rng.normal() * 0.1f * spacing;
      float half = 0.58f * patch * (1.f + 0.08f * st_.rng.normal());
      Vec2 seed = u.c + across * off;
      int each = std::max(1, static_cast<int>(half / sample));
      back.clear();
      pts.clear();
      Vec2 p = seed, heading = d * -1.f;
      for (int k = 0; k < each; ++k) {
        heading = steer(p, d * -1.f, heading);
        p = p + heading * sample;
        back.push_back(p);
      }
      for (size_t k = back.size(); k-- > 0;) pts.push_back(back[k]);
      pts.push_back(seed);
      p = seed;
      heading = d;
      for (int k = 0; k < each; ++k) {
        heading = steer(p, d, heading);
        p = p + heading * sample;
        pts.push_back(p);
      }
      if (i % 2 == 1) std::reverse(pts.begin(), pts.end());
      int ns = static_cast<int>(pts.size());
      int run_start = -1;
      float run_sum = 0.f;
      for (int k = 0; k <= ns; ++k) {
        float val = k < ns ? need_at(pts[k]) : -1.f;
        if (val > 0.3f) {
          if (run_start < 0) { run_start = k; run_sum = 0.f; }
          run_sum += val;
        } else if (run_start >= 0) {
          if ((k - run_start) * sample >= min_run)
            runs.push_back({std::vector<Vec2>(pts.begin() + run_start, pts.begin() + k), run_sum / (k - run_start)});
          run_start = -1;
        }
      }
    }
    if (runs.empty()) return false;
    // One charge of the brush for the patch.
    float area_mm2 = 0.f;
    for (const Run& r : runs) area_mm2 += path_length(r.pts) * mm_ * 0.42f * pass.brush_mm;
    if ((pass.wet_in_wet && skin_[u.region]) || sky_[u.region]) {
      // Skin is modeled wet in wet: the place is first dampened with clean
      // water, along the same strokes, so the color laid into it spreads
      // and dries without an edge. Sky and cloud are laid that way in every
      // layer: a sky has no edges at all.
      brush.wipe(0.f);
      brush.load_wet(PigVec{}, area_mm2 * coat * 1.5f + brush.tip_capacity_mm3());
      for (size_t i = 0; i < runs.size(); ++i) {
        Gesture g;
        g.path = run_path(runs[i].pts, 0.02f * (i % 2 ? -1.f : 1.f));
        g.press = 0.85f;
        brush.stroke(cv_, perform(g, hand, mm_, st_.rng));
        sim_.touch(brush.last_touched());
      }
    } else if (soft_[u.region]) {
      // A soft edge is made on damp paper. Where a stroke comes within the
      // width of the mark of a soft outline, that stretch is first gone over
      // with clean water, so the color laid there bleeds over the outline
      // and dries without an edge. The water is kept well away from hard
      // outlines, which must not bleed.
      const float keep_dry = 1.5f * line_px + 3.f / mm_;
      std::vector<std::vector<Vec2>> damp;
      for (const Run& r : runs) {
        size_t start = r.pts.size();
        for (size_t k = 0; k <= r.pts.size(); ++k) {
          bool near = k < r.pts.size() && outlines_.soft(cell_x(r.pts[k]), cell_y(r.pts[k])) < line_px &&
                      outlines_.dist(cell_x(r.pts[k]), cell_y(r.pts[k])) > keep_dry;
          if (near && start == r.pts.size()) start = k;
          if (!near && start != r.pts.size()) {
            if ((k - start) * sample >= 0.5f * min_run)
              damp.push_back(std::vector<Vec2>(r.pts.begin() + start, r.pts.begin() + k));
            start = r.pts.size();
          }
        }
      }
      if (!damp.empty()) {
        float damp_mm2 = 0.f;
        for (const auto& d_pts : damp) damp_mm2 += path_length(d_pts) * mm_ * 0.42f * pass.brush_mm;
        brush.wipe(0.f);
        brush.load_wet(PigVec{}, damp_mm2 * coat * 1.5f + brush.tip_capacity_mm3());
        for (size_t i = 0; i < damp.size(); ++i) {
          if (damp[i].size() < 2) continue;
          Gesture g;
          g.path = run_path(damp[i], 0.f);
          g.press = 0.85f;
          brush.stroke(cv_, perform(g, hand, mm_, st_.rng));
          sim_.touch(brush.last_touched());
        }
      }
    }
    brush.wipe(0.03f);
    brush.load_wet(per_mm, area_mm2 * coat * 1.5f + brush.tip_capacity_mm3());
    for (size_t i = 0; i < runs.size(); ++i) {
      Gesture g;
      g.path = run_path(runs[i].pts, 0.02f * (i % 2 ? -1.f : 1.f));
      g.press = clamp01(0.7f * std::clamp(runs[i].need, 0.6f, 1.2f) * st_.rng.range(0.92f, 1.08f));
      brush.stroke(cv_, perform(g, hand, mm_, st_.rng));
      sim_.touch(brush.last_touched());
      ++st_.strokes;
    }
    return true;
  }

  Studio& st_;
  Canvas& cv_;
  float mm_;
  WatercolorSim sim_;
  Mixer mixer_;
  Image target_;
  FlowField flow_;
  Plane<uint8_t> items_;  // with a scene: the item of each cell, as it is worked
  Outlines outlines_;  // with a scene: where the items' outlines run
  bool skin_[256] = {};  // with a scene: which items are skin
  bool sky_[256] = {};   // with a scene: which items are sky or cloud
  bool foliage_[256] = {};  // with a scene: which items are masses of foliage
  bool soft_[256] = {};  // with a scene: which items have soft edges
};

}  // namespace

void paint_watercolor(Studio& st) {
  Watercolorist w(st);
  float effort = st.settings.effort;
  float W = std::max(st.canvas.w, st.canvas.h) * st.canvas.mm_per_px;
  uint32_t seed = st.settings.seed * 977u;
  //                name               brush      patch      soften      thr   strength rounds sec  wet in wet on skin
  w.run_pass({"first washes",  W / 16.f,  W / 13.f,  W / 70.f,  0.024f, 0.75f, 1, 3.0f}, 0.00f, 0.18f, seed + 1);
  w.run_pass({"second washes", W / 30.f,  W / 28.f,  W / 160.f, 0.030f, 0.90f, 1, 2.5f, true}, 0.18f, 0.40f, seed + 2);
  w.run_pass({"shapes",        W / 55.f,  W / 55.f,  W / 400.f, 0.040f, 1.00f, effort >= 1.f ? 2 : 1, 2.0f, true}, 0.40f, 0.68f, seed + 3);
  if (effort >= 0.7f)
    w.run_pass({"details",     W / 110.f, W / 110.f, 0.f,       0.045f, 1.00f, 2, 1.5f, true}, 0.68f, 0.86f, seed + 4);
  if (effort >= 1.f)
    w.run_pass({"fine details", W / 190.f, W / 170.f, 0.f,      0.050f, 1.00f, effort >= 1.3f ? 2 : 1, 1.2f}, 0.86f, 1.00f, seed + 5);
}

}  // namespace bs
