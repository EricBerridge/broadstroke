// Drawing with dry media: colored pencil and soft pastel.
//
// The draftsman works the way one is taught to: lay tone in broad hatched
// patches, build it up in layers of smaller patches at changing angles,
// judging each patch against the subject before touching it, and last
// state the strongest edges with accents. For each patch the artist picks, from the sticks in the box, the one
// whose mark over what is already there would come closest to the subject
// (predicted from test swatches made on a scrap of the same paper), and
// presses harder where more is needed. Hatch lines stop where the canvas
// no longer needs them, so the hatching takes the shape of the forms.
#include <algorithm>
#include <iterator>
#include <map>
#include <memory>

#include "artist/paths.hpp"
#include "artist/studio.hpp"
#include "artist/target.hpp"
#include "media/dry_tool.hpp"

namespace bs {
namespace {

const int kLevels = 4;
const float kEfforts[kLevels] = {0.12f, 0.3f, 0.6f, 0.9f};

// One physical kind of tool (shape, hardness), whatever its color.
struct ToolKind {
  DrySpec proto;
  float reach_px = 0.f;         // how far from its position the tool can mark the sheet
  float cal_spacing_mm = 0.5f;  // hatch spacing of the test swatch
  float film[kLevels] = {};     // mean deposit thickness (mm) of that swatch at kEfforts
  float sharpen_mm3 = 0.f;      // re-point after this much wear; 0 = never
  int pieces = 1;               // identical pieces of each color at hand (a pastel stick is broken up)
};

struct Stick {
  std::string name;
  int kind = 0;
  PigVec recipe{};
};

enum class Dir { Fixed, Along, Across };

struct DryPass {
  std::string name;
  std::vector<int> sticks;
  float patch_mm = 6.f;
  float spacing_mm = 0.5f;
  Dir dir = Dir::Fixed;
  float angle = -0.785f;
  float threshold = 0.04f;
  int rounds = 1;
  float tempo = 1.3f;
  // A closing pass covers the paper where it still shows. That is craft,
  // not emphasis, so the tolerance is the same everywhere whatever the
  // detail and importance of the place; and a mark may be a dab as short
  // as one spot of bare paper.
  bool closing = false;
};

PigVec mix(std::initializer_list<std::pair<int, float>> parts) {
  PigVec r{};
  for (auto& p : parts) r[p.first] = p.second;
  return r;
}

class Draftsman {
 public:
  Draftsman(Studio& st) : st_(st), cv_(st.canvas), mm_(st.canvas.mm_per_px) {}

  int add_kind(const DrySpec& proto, float cal_spacing_mm, float sharpen_mm3, int pieces = 1) {
    ToolKind k;
    k.proto = proto;
    k.cal_spacing_mm = cal_spacing_mm;
    k.sharpen_mm3 = sharpen_mm3;
    k.pieces = std::max(pieces, 1);
    k.reach_px = DryTool(proto, mm_).reach_px();
    if (proto.wear > 0.f) calibrate(k);
    kinds_.push_back(k);
    return static_cast<int>(kinds_.size()) - 1;
  }
  int add_stick(const std::string& name, int kind, const PigVec& recipe) {
    Stick s;
    s.name = name; s.kind = kind; s.recipe = recipe;
    float t = sum(s.recipe);
    if (t > 0.f)
      for (float& v : s.recipe) v /= t;
    sticks_.push_back(s);
    return static_cast<int>(sticks_.size()) - 1;
  }

  // Tone-maps the subject into what this medium on this paper can show.
  // key > 1 keys the drawing lighter: mid-tones open up toward the paper
  // while the darks stay anchored, as a draftsman reserves the paper for
  // everything but the shadows.
  void prepare_target(float chroma, float key, float emphasis) {
    Lab paper = rgb_to_oklab(spectrum_to_rgb(cv_.surface.reflectance));
    float l_dark = paper.L, l_top = paper.L;
    for (const Stick& s : sticks_) {
      const ToolKind& k = kinds_[s.kind];
      if (k.proto.wear <= 0.f) continue;
      PigVec a{};
      for (int i = 0; i < kMaxPig; ++i) a[i] = s.recipe[i] * k.proto.pigment_load * k.film[kLevels - 1] * 2.5f;
      Spectrum kd, sd;
      cv_.palette.film(a, kd, sd);
      float L = rgb_to_oklab(spectrum_to_rgb(km_over(kd, sd, cv_.surface.reflectance))).L;
      l_dark = std::min(l_dark, L);
      l_top = std::max(l_top, L);
    }
    ToneIntent intent;
    intent.l_dark = l_dark;
    intent.l_top = l_top;
    intent.chroma = chroma;
    intent.key = key;
    intent.emphasis = emphasis;
    target_ = make_target(st_.subject, intent, mm_);
    flow_ = compute_flow(st_.subject.color, std::max(2.f, 1.6f / mm_));
    note_range_ = "tonal range L " + std::to_string(l_dark).substr(0, 4) + " to " + std::to_string(l_top).substr(0, 4);
  }

  // Accents: the last, linear touches along the strongest edges of the
  // subject. An accent is judged like any other mark. Along each edge the
  // artist looks for the stretch where the drawing is still lighter than the
  // subject, picks the stick and the pressure that would bring that stretch
  // closest, and draws only there. Where the drawing already has the edge,
  // or no stick would improve it, no line is made: a dark line on a form
  // that is not dark there is the mark of a careless hand.
  //
  // `firm` is how much darker than the subject an accent may state an edge
  // (in lightness). At 0 an accent only supplies a dark the drawing lacks.
  // Above 0 the line is moved into the dark beside the edge and that dark is
  // wanted `firm` darker than the subject has it, so every strong edge gets
  // a soft line in the color of its own dark side.
  void accents(const std::string& name, const std::vector<int>& sticks, float lo, float hi, float min_edge,
               float cell_mm, float firm = 0.f) {
    st_.begin_stage(name, lo, hi);
    int before = st_.strokes;
    Plane<uint8_t> drawn((cv_.w + 3) / 4, (cv_.h + 3) / 4, 0);
    std::vector<EdgeSeed> seeds = edge_seeds(min_edge, cell_mm);
    const float short_of = 0.03f;                   // lightness the drawing must lack for an accent
    const float min_run = std::max(3.f, 2.f / mm_);  // px
    std::vector<Lab> want, have;
    for (size_t i = 0; i < seeds.size() && !st_.cancelled; ++i) {
      std::vector<Vec2> traced = trace_edge(seeds[i], drawn);
      if (traced.empty()) continue;
      std::vector<Vec2> path = smooth_path(traced, 2);
      const int n = static_cast<int>(path.size());
      if (firm > 0.f) {
        // An edge is where light meets dark. The line goes in the dark
        // beside it (the crease, the gap between two forms).
        const float reach = std::max(1.5f, 0.5f / mm_), shift = std::max(1.f, 0.25f / mm_);
        std::vector<Vec2> nrm(path.size());
        float lighter = 0.f;  // > 0: the side the normals point to is the lighter one
        for (int k = 0; k < n; ++k) {
          nrm[k] = perp(normalize(path[std::min(k + 1, n - 1)] - path[std::max(k - 1, 0)]));
          lighter += rgb_to_oklab(st_.glance(target_, path[k] + nrm[k] * reach, 1.f)).L -
                     rgb_to_oklab(st_.glance(target_, path[k] - nrm[k] * reach, 1.f)).L;
        }
        const float to = lighter > 0.f ? -shift : shift;
        for (int k = 0; k < n; ++k) path[k] = path[k] + nrm[k] * to;
      }
      // What is missing along the line, on average over the part that is
      // short of the subject's dark.
      want.assign(path.size(), Lab{});
      have.assign(path.size(), Lab{});
      Lab mw{0.f, 0.f, 0.f}, mh{0.f, 0.f, 0.f};
      Spectrum base = spectrum_fill(0.f);
      int lacking = 0;
      for (int k = 0; k < n; ++k) {
        want[k] = rgb_to_oklab(st_.glance(target_, path[k], 1.5f));
        want[k].L -= firm;
        have[k] = rgb_to_oklab(st_.glance(st_.seen, path[k], 1.5f));
        if (have[k].L - want[k].L < short_of) continue;
        mw.L += want[k].L; mw.a += want[k].a; mw.b += want[k].b;
        mh.L += have[k].L; mh.a += have[k].a; mh.b += have[k].b;
        Spectrum r = cv_.reflectance(std::clamp(static_cast<int>(path[k].x), 0, cv_.w - 1),
                                     std::clamp(static_cast<int>(path[k].y), 0, cv_.h - 1));
        for (int b = 0; b < kBands; ++b) base[b] += r[b];
        ++lacking;
      }
      if (lacking < 2) continue;
      const float inv = 1.f / lacking;
      mw = {mw.L * inv, mw.a * inv, mw.b * inv};
      mh = {mh.L * inv, mh.a * inv, mh.b * inv};
      for (int b = 0; b < kBands; ++b) base[b] *= inv;
      // Which stick, and how hard, comes closest.
      Lab base_lab = rgb_to_oklab(spectrum_to_rgb(base));
      const float err = lab_dist(mw, mh);
      float best = err;
      int stick = -1, eidx = 0;
      Lab delta{0.f, 0.f, 0.f};
      for (int si : sticks) {
        const Stick& sk = sticks_[si];
        const ToolKind& kd_ = kinds_[sk.kind];
        for (int e = 0; e < kLevels; ++e) {
          PigVec a{};
          for (int c = 0; c < kMaxPig; ++c) a[c] = sk.recipe[c] * kd_.film[e] * kd_.proto.pigment_load;
          Spectrum kd, sd;
          cv_.palette.film(a, kd, sd);
          Lab pred = rgb_to_oklab(spectrum_to_rgb(km_over(kd, sd, base)));
          Lab d{pred.L - base_lab.L, pred.a - base_lab.a, pred.b - base_lab.b};
          Lab after{mh.L + d.L, mh.a + d.a, mh.b + d.b};
          float score = lab_dist(after, mw);
          if (score < best) { best = score; stick = si; eidx = e; delta = d; }
        }
      }
      if (stick < 0 || err - best < std::max(0.15f * err, 0.008f)) continue;
      // Draw the stretches where that mark is wanted, pressing as each
      // point needs.
      const float d2 = std::max(sqr(delta.L) + sqr(delta.a) + sqr(delta.b), 1e-8f);
      DryTool& tool = tool_for(stick);
      HandParams hand = tool.hand();
      hand.cruise_mm_s = 90.f;
      int run_start = -1;
      for (int k = 0; k <= n; ++k) {
        float val = -1.f;
        if (k < n)
          val = ((want[k].L - have[k].L) * delta.L + (want[k].a - have[k].a) * delta.a +
                 (want[k].b - have[k].b) * delta.b) / d2;
        if (val > 0.3f) {
          if (run_start < 0) run_start = k;
          continue;
        }
        if (run_start < 0) continue;
        Gesture gst;
        gst.path.assign(path.begin() + run_start, path.begin() + k);
        if (gst.path.size() >= 2 && path_length(gst.path) >= min_run) {
          for (int j = run_start; j < k; ++j) {
            float v = ((want[j].L - have[j].L) * delta.L + (want[j].a - have[j].a) * delta.a +
                       (want[j].b - have[j].b) * delta.b) / d2;
            gst.pressure.push_back(clamp01(kEfforts[eidx] * std::clamp(v, 0.45f, 1.5f)));
          }
          maintain(tool, stick);
          tool.stroke(cv_, perform(gst, hand, mm_, st_.rng));
          ++st_.strokes;
        }
        run_start = -1;
      }
      st_.look();
      st_.report(static_cast<float>(i) / seeds.size());
    }
    st_.look();
    st_.note(name + ": " + std::to_string(st_.strokes - before) + " lines");
  }

  // Tonal / color build-up by hatched patches.
  //
  // Patches far enough apart can neither see nor touch each other's marks,
  // so they are worked at the same time, in waves. Each patch of a wave is
  // first planned from the sheet as it stands: which stick, how hard, where
  // the hatch lines run, and the hand's motion along them. Then the planned
  // strokes are made. Patches that use the same stick are made one after
  // another, in their order, because a stick carries its worn point from one
  // patch to the next; different sticks work in parallel. Where the artist
  // has several pieces of a color (a broken pastel stick), the patches of
  // that color are dealt out among the pieces in turn. Every patch draws
  // its random numbers from a stream of its own, so the drawing does not
  // depend on how the work happens to be scheduled.
  void hatch_pass(const DryPass& pass, float lo, float hi) {
    st_.begin_stage(pass.name, lo, hi);
    int before = st_.strokes;
    const float patch = pass.patch_mm / mm_;
    const float spacing = pass.spacing_mm / mm_;
    float reach = 0.f;
    for (int si : pass.sticks) reach = std::max(reach, kinds_[sticks_[si].kind].reach_px);
    // What a patch is likely to look at or mark; only used to pick a wave.
    // What it really needs is known once it is planned.
    const float likely = 1.25f * patch + reach + 3.f / mm_;
    // Coarse grid for testing whether patches are clear of each other.
    const int cell = std::max(4, static_cast<int>(0.25f * patch));
    const int gw = cv_.w / cell + 1, gh = cv_.h / cell + 1;
    std::vector<uint8_t> grid(static_cast<size_t>(gw) * gh);
    auto cells_of = [&](const Rect& box) {
      Rect r = box.clipped(cv_.w, cv_.h);
      if (r.empty()) return Rect{};
      return Rect{r.x0 / cell, r.y0 / cell, (r.x1 - 1) / cell + 1, (r.y1 - 1) / cell + 1};
    };
    auto is_free = [&](const Rect& box) {
      Rect g = cells_of(box);
      for (int y = g.y0; y < g.y1; ++y)
        for (int x = g.x0; x < g.x1; ++x)
          if (grid[static_cast<size_t>(y) * gw + x]) return false;
      return true;
    };
    auto claim = [&](const Rect& box) {
      Rect g = cells_of(box);
      for (int y = g.y0; y < g.y1; ++y)
        for (int x = g.x0; x < g.x1; ++x) grid[static_cast<size_t>(y) * gw + x] = 1;
    };

    for (int round = 0; round < pass.rounds && !st_.cancelled; ++round) {
      st_.look();
      std::vector<Vec2> centers;
      float stepc = patch * 0.85f;
      for (float y = 0.5f * stepc; y < cv_.h + 0.4f * stepc; y += stepc)
        for (float x = 0.5f * stepc; x < cv_.w + 0.4f * stepc; x += stepc)
          centers.push_back({std::clamp(x + st_.rng.range(-0.2f, 0.2f) * patch, 1.f, cv_.w - 2.f),
                             std::clamp(y + st_.rng.range(-0.2f, 0.2f) * patch, 1.f, cv_.h - 2.f)});
      // Wander over the sheet rather than scanning it.
      for (size_t i = centers.size(); i > 1; --i) std::swap(centers[i - 1], centers[st_.rng.below(static_cast<int>(i))]);
      // Working by item, a patch that lies on several items is worked once
      // for each: judged at a point inside that item, its marks kept to it.
      std::vector<Vec2> judged;
      std::vector<int> regions;
      if (by_item_) {
        std::vector<Vec2> all;
        all.swap(centers);
        for (Vec2 c : all) add_units(c, stepc, centers, judged, regions);
      }
      const int count = static_cast<int>(centers.size());
      std::vector<uint64_t> seeds(centers.size());
      for (uint64_t& seed : seeds) {
        uint64_t hi_bits = st_.rng.next();
        seed = (hi_bits << 32) | st_.rng.next();
      }

      std::vector<int> pending(centers.size());
      for (int i = 0; i < count; ++i) pending[i] = i;
      int done = 0;
      std::vector<PatchPlan> plans;
      while (!pending.empty() && !st_.cancelled) {
        // A wave: patches, taken in order, that look clear of each other.
        std::vector<int> wave, rest;
        std::fill(grid.begin(), grid.end(), static_cast<uint8_t>(0));
        for (int i : pending) {
          Vec2 c = centers[i];
          Rect box{static_cast<int>(c.x - likely), static_cast<int>(c.y - likely), static_cast<int>(c.x + likely) + 1,
                   static_cast<int>(c.y + likely) + 1};
          if (is_free(box)) {
            claim(box);
            wave.push_back(i);
          } else {
            rest.push_back(i);
          }
        }
        // Plan them all from the sheet as it stands.
        const int n_wave = static_cast<int>(wave.size());
        plans.assign(wave.size(), PatchPlan{});
        parallel_for(0, n_wave, [&](int k0, int k1) {
          for (int k = k0; k < k1; ++k)
            plans[k] = by_item_ ? plan_patch(centers[wave[k]], judged[wave[k]], regions[wave[k]], patch, spacing,
                                             pass, round, seeds[wave[k]])
                                : plan_patch(centers[wave[k]], centers[wave[k]], -1, patch, spacing, pass, round,
                                             seeds[wave[k]]);
        }, 1);
        // Keep the ones whose plans really are clear of each other; the
        // others wait for a later wave and are planned afresh then.
        std::fill(grid.begin(), grid.end(), static_cast<uint8_t>(0));
        // (stick, piece) -> its patches of this wave, in order
        std::map<std::pair<int, int>, std::vector<int>> by_tool;
        std::map<int, int> dealt;  // stick -> patches of this wave dealt out so far
        std::vector<int> waiting;
        for (int k = 0; k < n_wave; ++k) {
          PatchPlan& plan = plans[k];
          if (plan.motions.empty()) {
            ++done;  // nothing wanted here
          } else if (is_free(plan.box)) {
            claim(plan.box);
            plan.piece = dealt[plan.stick]++ % kinds_[sticks_[plan.stick].kind].pieces;
            by_tool[{plan.stick, plan.piece}].push_back(k);
            ++done;
          } else {
            waiting.push_back(wave[k]);
          }
        }
        pending.clear();
        std::merge(waiting.begin(), waiting.end(), rest.begin(), rest.end(), std::back_inserter(pending));

        // Make the strokes: the patches of one stick (or piece) in order,
        // different ones at the same time.
        std::vector<const std::vector<int>*> chains;
        for (auto& kv : by_tool) {
          tool_for(kv.first.first, kv.first.second);  // take it out of the box before the work starts
          chains.push_back(&kv.second);
        }
        const int n_chains = static_cast<int>(chains.size());
        std::vector<Rect> changed(chains.size());
        std::vector<int> made(chains.size(), 0);
        parallel_for(0, n_chains, [&](int g0, int g1) {
          for (int g = g0; g < g1; ++g)
            for (int k : *chains[g]) made[g] += work_patch(plans[k], changed[g]);
        }, 1);
        for (int g = 0; g < n_chains; ++g) {
          cv_.note_dirty(changed[g]);
          st_.strokes += made[g];
        }
        st_.look();
        st_.report((round + static_cast<float>(done) / count) / pass.rounds);
      }
    }
    st_.note(pass.name + ": " + std::to_string(st_.strokes - before) + " strokes");
  }

  // Softens toned passages with a stump or fingertip, in smooth areas only.
  void blend(const std::string& name, int stick, float patch_mm, float lo, float hi, float max_detail) {
    st_.begin_stage(name, lo, hi);
    int before = st_.strokes;
    DryTool& tool = tool_for(stick);
    HandParams hand = tool.hand();
    hand.cruise_mm_s = 160.f;
    const float patch = patch_mm / mm_;
    int nx = std::max(1, static_cast<int>(cv_.w / patch)), ny = std::max(1, static_cast<int>(cv_.h / patch));
    for (int j = 0; j < ny && !st_.cancelled; ++j)
      for (int i = 0; i < nx && !st_.cancelled; ++i) {
        Vec2 c{(i + 0.5f) * cv_.w / nx, (j + 0.5f) * cv_.h / ny};
        int x = static_cast<int>(c.x), y = static_cast<int>(c.y);
        if (st_.subject.detail(x, y) > max_detail) continue;
        if (cv_.vol(x, y) + cv_.vol(std::min(x + 3, cv_.w - 1), y) + cv_.vol(x, std::min(y + 3, cv_.h - 1)) <= 0.f)
          continue;
        Vec2 d = normalize(Vec2{std::cos(-0.785f + st_.rng.normal() * 0.4f), std::sin(-0.785f + st_.rng.normal() * 0.4f)});
        float sp = std::max(2.f, 0.45f * tool.spec.core_mm / mm_);
        Gesture g;
        g.path = zigzag_path(c, d, patch * 1.2f, sp, std::max(2, static_cast<int>(patch * 1.1f / sp)), 0.04f, st_.rng);
        g.press = 0.55f;
        g.tempo = 1.2f;
        if (by_item_) {
          // Near an outline the finger rubs along it, not across, and stays
          // on its own side: pastel is not dragged from one item into the
          // next, and the tooth of the paper is still filled up to the edge.
          if (outlines_.dist(x, y) < patch) {
            Vec2 along = outlines_.along(x, y);
            g.path = zigzag_path(c, along, patch * 1.2f, sp, std::max(2, static_cast<int>(patch * 1.1f / sp)), 0.04f,
                                 st_.rng);
          }
          if (!keep_within(g.path, region_at(c), 0.15f * tool.spec.core_mm / mm_)) continue;
        }
        tool.stroke(cv_, perform(g, hand, mm_, st_.rng));
        ++st_.strokes;
        st_.report(static_cast<float>(j * nx + i) / (nx * ny));
      }
    st_.look();
    st_.note(name + ": " + std::to_string(st_.strokes - before) + " strokes");
  }

  void fix() {
    // A spray of fixative: the layer is bound and later work sits on top of it.
    cv_.dry(1.f);
    st_.look();
  }

  std::vector<int> all_sticks_of(int kind) const {
    std::vector<int> v;
    for (size_t i = 0; i < sticks_.size(); ++i)
      if (sticks_[i].kind == kind) v.push_back(static_cast<int>(i));
    return v;
  }
  const std::string& range_note() const { return note_range_; }

  // With a scene, work by item (spec 2.13): each patch is judged within the
  // item at its center, and its marks end at that item's outline.
  bool by_item() const { return by_item_; }
  // `overrun_mm`: how far a hatch line may run past the outline. A thin
  // point lands and lifts at the ends of a line, so lines that end exactly
  // on the outline from both sides leave a pale rim along it.
  void work_by_item(float overrun_mm = 0.f) {
    if (st_.subject.item.empty()) return;
    by_item_ = true;
    overrun_px_ = overrun_mm / mm_;
    outlines_ = find_outlines(st_.subject.item);
  }

 private:
  // Makes a test swatch on a scrap of the same paper and measures how much
  // material a hatched patch of this tool lays down at each effort.
  void calibrate(ToolKind& k) {
    const int n = std::clamp(static_cast<int>(std::max(12.f, 2.5f * k.proto.bar_len_mm * (k.proto.tip == DrySpec::Tip::Bar ? 1.f : 0.f)) / mm_), 40, 150);
    Rng rng(4242);
    for (int e = 0; e < kLevels; ++e) {
      Canvas scrap(make_surface(cv_.surface.name, n, n, mm_, st_.settings.seed + 77u), cv_.palette);
      DrySpec spec = k.proto;
      spec.recipe = PigVec{};
      spec.recipe[0] = 1.f;
      Vec2 d = normalize(Vec2{1.f, -1.f});
      if (spec.tip == DrySpec::Tip::Bar) spec.lean = perp(d);
      DryTool tool(spec, mm_);
      HandParams hand = tool.hand();
      float sp = k.cal_spacing_mm / mm_;
      int lines = static_cast<int>(1.5f * n / sp);
      for (int i = 0; i < lines; ++i) {
        Vec2 m = Vec2{0.5f * n, 0.5f * n} + perp(d) * ((i - 0.5f * lines) * sp);
        Gesture g;
        g.path = arc_path(m - d * (0.7f * n), m + d * (0.7f * n), 0.f, 6);
        g.press = kEfforts[e];
        g.tempo = 1.3f;
        tool.stroke(scrap, perform(g, hand, mm_, rng));
        if (k.sharpen_mm3 > 0.f && tool.worn_mm3() > k.sharpen_mm3) tool.sharpen();
      }
      double acc = 0.0;
      int cnt = 0;
      for (int y = n / 4; y < 3 * n / 4; ++y)
        for (int x = n / 4; x < 3 * n / 4; ++x) { acc += scrap.vol(x, y); ++cnt; }
      k.film[e] = static_cast<float>(acc / std::max(cnt, 1));
    }
  }

  // Where the subject's strongest edges are: the strongest point of each
  // cell of a grid, if it is strong enough, strongest first.
  struct EdgeSeed { Vec2 p; float e; };
  std::vector<EdgeSeed> edge_seeds(float min_edge, float cell_mm) const {
    const int g = std::max(4, static_cast<int>(cell_mm / mm_));
    std::vector<EdgeSeed> seeds;
    for (int gy = 0; gy * g < cv_.h; ++gy)
      for (int gx = 0; gx * g < cv_.w; ++gx) {
        float best = 0.f;
        Vec2 bp;
        for (int y = gy * g; y < std::min((gy + 1) * g, cv_.h); y += 2)
          for (int x = gx * g; x < std::min((gx + 1) * g, cv_.w); x += 2) {
            float e = st_.subject.edge(x, y);
            if (e > best) { best = e; bp = {x + 0.5f, y + 0.5f}; }
          }
        if (best >= min_edge) seeds.push_back({bp, best});
      }
    std::sort(seeds.begin(), seeds.end(), [](const EdgeSeed& a, const EdgeSeed& b) { return a.e > b.e; });
    return seeds;
  }

  // Follows the edge through a seed for as long as it stays strong, and
  // notes on `drawn` (a quarter-size map) that this edge is taken. Returns
  // nothing if the seed is on an edge already taken or the edge is too short.
  std::vector<Vec2> trace_edge(const EdgeSeed& seed, Plane<uint8_t>& drawn) {
    Vec2 p = seed.p;
    if (drawn(static_cast<int>(p.x) / 4, static_cast<int>(p.y) / 4)) return {};
    float e0 = seed.e;
    TraceParams tp;
    tp.step = std::max(2.f, 0.7f / mm_);
    tp.max_len = 45.f / mm_;
    tp.min_len = 2.5f / mm_;
    tp.inertia = 0.3f;
    auto keep = [&](Vec2 q) {
      int x = static_cast<int>(q.x), y = static_cast<int>(q.y);
      if (drawn(x / 4, y / 4)) return false;
      return st_.subject.edge(x, y) > 0.45f * e0;
    };
    std::vector<Vec2> path = trace_flow(flow_, p, tp, keep, st_.rng);
    if (path_length(path) * mm_ < 2.f) return {};
    for (const Vec2& q : path) {
      int cx = static_cast<int>(q.x) / 4, cy = static_cast<int>(q.y) / 4;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if (drawn.inside(cx + dx, cy + dy)) drawn(cx + dx, cy + dy) = 1;
    }
    return path;
  }

  DryTool& tool_for(int stick, int piece = 0) {
    auto it = tools_.find({stick, piece});
    if (it != tools_.end()) return *it->second;
    DrySpec spec = kinds_[sticks_[stick].kind].proto;
    spec.recipe = sticks_[stick].recipe;
    auto t = std::make_unique<DryTool>(spec, mm_);
    DryTool& ref = *t;
    tools_[{stick, piece}] = std::move(t);
    return ref;
  }

  void maintain(DryTool& tool, int stick) const {
    float lim = kinds_[sticks_[stick].kind].sharpen_mm3;
    if (lim > 0.f && tool.worn_mm3() > lim) tool.sharpen();
  }

  Vec2 direction(Vec2 c, const DryPass& pass, int round, Rng& rng) const {
    float ang = pass.angle + 0.5f * round + rng.normal() * 0.12f;
    Vec2 fixed{std::cos(ang), std::sin(ang)};
    if (pass.dir == Dir::Fixed) return fixed;
    int x = std::clamp(static_cast<int>(c.x), 0, cv_.w - 1), y = std::clamp(static_cast<int>(c.y), 0, cv_.h - 1);
    if (flow_.coherence(x, y) < 0.2f || flow_.strength(x, y) < 0.1f) return fixed;
    Vec2 d = flow_.dir(x, y);
    if (pass.dir == Dir::Across) d = perp(d);
    d = rotate(d, rng.normal() * 0.1f + 0.35f * round);
    return d;
  }

  // The items a patch covers: for each, the patch's center, a point of the
  // patch inside the item, and the item. An item that covers less than a
  // twelfth of the patch is left to a pass with smaller patches.
  void add_units(Vec2 c, float stepc, std::vector<Vec2>& centers, std::vector<Vec2>& judged,
                 std::vector<int>& regions) const {
    const Plane<uint8_t>& item = st_.subject.item;
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
      if (found[f].n < 2) continue;
      Vec2 mean = found[f].sum * (1.f / found[f].n);
      int best = -1;
      float best_d = 0.f;
      for (int k = 0; k < kCount; ++k) {
        if (ids[k] != found[f].id) continue;
        float d = length(pts[k] - mean);
        if (best < 0 || d < best_d) { best = k; best_d = d; }
      }
      centers.push_back(c);
      judged.push_back(pts[best]);
      regions.push_back(found[f].id);
    }
  }

  // The item a point belongs to when working by item; -1 otherwise.
  int region_at(Vec2 p) const {
    if (!by_item_) return -1;
    return st_.subject.item(std::clamp(static_cast<int>(p.x), 0, cv_.w - 1),
                            std::clamp(static_cast<int>(p.y), 0, cv_.h - 1));
  }

  // Whether a mark of the given half-width made at p stays inside the item.
  bool inside(Vec2 p, int region, float half_width) const {
    if (region < 0) return true;
    if (!(p.x >= 0.f && p.y >= 0.f && p.x < cv_.w && p.y < cv_.h)) return false;
    int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
    return st_.subject.item(x, y) == region && outlines_.dist(x, y) >= half_width;
  }

  // Whether p is within `reach` px of an item's outline (never, if reach is 0).
  bool near_outline(Vec2 p, float reach) const {
    return reach > 0.f && outlines_.dist(static_cast<int>(p.x), static_cast<int>(p.y)) < reach;
  }

  // Shortens a path to the stretch nearest its middle that stays inside the
  // item. Returns false if less than a quarter of it is left.
  bool keep_within(std::vector<Vec2>& path, int region, float half_width) const {
    if (region < 0 || path.size() < 2) return true;
    // Start from the point inside the item that is nearest the middle.
    size_t mid = path.size() / 2, start = path.size();
    for (size_t off = 0; off <= mid && start == path.size(); ++off) {
      if (inside(path[mid - off], region, half_width)) start = mid - off;
      else if (mid + off < path.size() && inside(path[mid + off], region, half_width)) start = mid + off;
    }
    if (start == path.size()) return false;
    size_t a = start, b = start;
    while (a > 0 && inside(path[a - 1], region, half_width)) --a;
    while (b + 1 < path.size() && inside(path[b + 1], region, half_width)) ++b;
    if (b - a + 1 < path.size() / 4) return false;
    path = std::vector<Vec2>(path.begin() + a, path.begin() + b + 1);
    return path.size() >= 2;
  }

  // The artist's glance at a spot, looking only at one item (any, if -1).
  Rgb glance_in(const Image& img, Vec2 p, float radius, int region) const {
    if (region < 0) return st_.glance(img, p, radius);
    static const float off[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                    {0.7f, 0.7f}, {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};
    Rgb acc;
    int n = 0;
    for (auto& o : off) {
      int x = std::clamp(static_cast<int>(p.x + o[0] * radius), 0, img.w - 1);
      int y = std::clamp(static_cast<int>(p.y + o[1] * radius), 0, img.h - 1);
      if (st_.subject.item(x, y) != region) continue;
      acc += img(x, y);
      ++n;
    }
    if (n == 0) return st_.glance(img, p, 0.f);
    return acc * (1.f / n);
  }

  Spectrum mean_reflectance(Vec2 c, float r, int region = -1) const {
    static const float off[5][2] = {{0, 0}, {0.6f, 0.2f}, {-0.6f, -0.2f}, {0.2f, -0.6f}, {-0.2f, 0.6f}};
    Spectrum acc = spectrum_fill(0.f);
    for (auto& o : off) {
      int x = std::clamp(static_cast<int>(c.x + o[0] * r), 0, cv_.w - 1);
      int y = std::clamp(static_cast<int>(c.y + o[1] * r), 0, cv_.h - 1);
      if (region >= 0 && st_.subject.item(x, y) != region) {
        // Outside the item: look at the center instead.
        x = std::clamp(static_cast<int>(c.x), 0, cv_.w - 1);
        y = std::clamp(static_cast<int>(c.y), 0, cv_.h - 1);
      }
      Spectrum s = cv_.reflectance(x, y);
      for (int b = 0; b < kBands; ++b) acc[b] += s[b] * 0.2f;
    }
    return acc;
  }

  // Which stick, and how hard? Returns false if the patch needs nothing.
  bool decide(Vec2 c, int region, float patch, const DryPass& pass, int& stick, int& eidx, Lab& delta) const {
    Lab want = rgb_to_oklab(glance_in(target_, c, patch * 0.3f, region));
    Lab have = rgb_to_oklab(glance_in(st_.seen, c, patch * 0.3f, region));
    float err = lab_dist(want, have);
    float detail = pass.closing ? 0.5f
                                  : st_.subject.detail(std::clamp(static_cast<int>(c.x), 0, cv_.w - 1),
                                                       std::clamp(static_cast<int>(c.y), 0, cv_.h - 1));
    if (err < pass.threshold / (1.f + 0.8f * detail)) return false;
    // A patch is only worked if its neighborhood agrees that something is
    // missing; otherwise borderline areas break up into isolated blocks.
    float err_wide = lab_dist(rgb_to_oklab(glance_in(target_, c, patch * 1.1f, region)),
                              rgb_to_oklab(glance_in(st_.seen, c, patch * 1.1f, region)));
    if (err_wide < 0.6f * pass.threshold / (1.f + 0.8f * detail)) return false;
    Spectrum base = mean_reflectance(c, patch * 0.3f, region);
    Lab base_lab = rgb_to_oklab(spectrum_to_rgb(base));
    float best = err;
    stick = -1;
    for (int si : pass.sticks) {
      const Stick& s = sticks_[si];
      const ToolKind& k = kinds_[s.kind];
      for (int e = 0; e < kLevels; ++e) {
        float amount = k.film[e] * std::min(k.cal_spacing_mm / pass.spacing_mm, 1.6f) * k.proto.pigment_load;
        PigVec a{};
        for (int i = 0; i < kMaxPig; ++i) a[i] = s.recipe[i] * amount;
        Spectrum kd, sd;
        cv_.palette.film(a, kd, sd);
        Lab pred = rgb_to_oklab(spectrum_to_rgb(km_over(kd, sd, base)));
        Lab d{pred.L - base_lab.L, pred.a - base_lab.a, pred.b - base_lab.b};
        Lab after{have.L + d.L, have.a + d.a, have.b + d.b};
        float score = lab_dist(after, want);
        if (score < best) { best = score; stick = si; eidx = e; delta = d; }
      }
    }
    if (stick < 0) return false;
    return err - best >= std::max(0.15f * err, 0.008f);
  }

  // A patch thought through before it is touched: the stick, the direction
  // of the hatching, the hand's motion for every stroke, and the region of
  // the sheet the patch looks at or may mark.
  struct PatchPlan {
    int stick = -1;
    int piece = 0;  // which of the artist's pieces of that color makes it
    Vec2 dir{1.f, 0.f};
    std::vector<std::vector<Pose>> motions;
    Rect box;
  };

  // Plans a patch from the sheet as last looked at. Reads only, so several
  // patches can be planned at once.
  // `c` is the center of the patch. `at` is where it is judged and `region`
  // the item it is kept to; without a scene these are c and -1.
  PatchPlan plan_patch(Vec2 c, Vec2 at, int region, float patch, float spacing, const DryPass& pass, int round,
                       uint64_t seed) const {
    PatchPlan plan;
    Rng rng(seed);
    int eidx = 0;
    Lab delta;
    if (!decide(at, region, patch, pass, plan.stick, eidx, delta)) return plan;
    const Vec2 d = direction(at, pass, round, rng);
    plan.dir = d;
    const ToolKind& kind = kinds_[sticks_[plan.stick].kind];
    const HandParams hand = dry_hand(kind.proto);
    const float effort = kEfforts[eidx];
    const float d2 = std::max(sqr(delta.L) + sqr(delta.a) + sqr(delta.b), 1e-8f);
    const float min_run = pass.closing ? 2.f : std::max(3.f, 1.2f / mm_);
    const float sample = 2.f;
    // Working by item, a hatch line ends where its middle would leave the
    // item. The mark itself may lap over the outline by half its width;
    // held further back, the marks left the paper bare along every outline.
    Vec2 across = perp(d);
    // Extent of what is looked at (the patch and the hatch lines) and of
    // where the tool goes.
    float lx0 = c.x - 1.1f * patch, ly0 = c.y - 1.1f * patch, lx1 = c.x + 1.1f * patch, ly1 = c.y + 1.1f * patch;
    float tx0 = 1e9f, ty0 = 1e9f, tx1 = -1e9f, ty1 = -1e9f;
    int n = std::max(1, static_cast<int>(std::ceil(patch * 1.05f / spacing)));
    for (int i = 0; i < n; ++i) {
      float off = (i - 0.5f * (n - 1)) * spacing + rng.normal() * 0.18f * spacing;
      float half = 0.6f * patch * (1.f + 0.12f * rng.normal());
      Vec2 a = c + across * off - d * half, b = c + across * off + d * half;
      if (i % 2 == 1) std::swap(a, b);  // the hand goes back and forth
      lx0 = std::min({lx0, a.x, b.x}); ly0 = std::min({ly0, a.y, b.y});
      lx1 = std::max({lx1, a.x, b.x}); ly1 = std::max({ly1, a.y, b.y});
      Vec2 dir = normalize(b - a);
      float len = length(b - a);
      int ns = std::max(2, static_cast<int>(len / sample));
      // Find the runs along this line where the mark is still wanted.
      int run_start = -1;
      float run_sum = 0.f;
      for (int k = 0; k <= ns; ++k) {
        float val = -1.f;
        if (k < ns) {
          Vec2 p = a + dir * (k * sample);
          if (p.x >= 0.f && p.y >= 0.f && p.x < cv_.w && p.y < cv_.h && (inside(p, region, 0.f) || near_outline(p, overrun_px_))) {
            Lab t = rgb_to_oklab(st_.glance(target_, p, 1.5f));
            Lab s = rgb_to_oklab(st_.glance(st_.seen, p, 1.5f));
            val = ((t.L - s.L) * delta.L + (t.a - s.a) * delta.a + (t.b - s.b) * delta.b) / d2;
          }
        }
        if (val > 0.3f) {
          if (run_start < 0) { run_start = k; run_sum = 0.f; }
          run_sum += val;
        } else if (run_start >= 0) {
          float run_len = (k - run_start) * sample;
          if (run_len >= min_run) {
            float mean = run_sum / (k - run_start);
            Gesture g;
            Vec2 p0 = a + dir * (run_start * sample), p1 = a + dir * (k * sample);
            g.path = arc_path(p0, p1, 0.025f * (i % 2 ? -1.f : 1.f), std::max(4, static_cast<int>(run_len / 6.f)));
            g.press = clamp01(effort * std::clamp(mean, 0.45f, 1.5f) * rng.range(0.9f, 1.1f));
            g.tempo = pass.tempo;
            plan.motions.push_back(perform(g, hand, mm_, rng));
            for (const Pose& pose : plan.motions.back()) {
              tx0 = std::min(tx0, pose.pos.x); ty0 = std::min(ty0, pose.pos.y);
              tx1 = std::max(tx1, pose.pos.x); ty1 = std::max(ty1, pose.pos.y);
            }
          }
          run_start = -1;
        }
      }
    }
    if (plan.motions.empty()) return plan;
    const float look = 3.f, mark = kind.reach_px;
    plan.box = Rect{static_cast<int>(std::floor(std::min(lx0 - look, tx0 - mark))),
                    static_cast<int>(std::floor(std::min(ly0 - look, ty0 - mark))),
                    static_cast<int>(std::ceil(std::max(lx1 + look, tx1 + mark))) + 1,
                    static_cast<int>(std::ceil(std::max(ly1 + look, ty1 + mark))) + 1};
    return plan;
  }

  // Makes the strokes of a planned patch with its stick. Returns how many.
  int work_patch(const PatchPlan& plan, Rect& changed) {
    DryTool& tool = *tools_.at({plan.stick, plan.piece});
    if (tool.spec.tip == DrySpec::Tip::Bar) {
      // A stick used on its side is held across the direction of the stroke.
      Vec2 want_lean = perp(plan.dir);
      if (std::abs(dot(tool.spec.lean, want_lean)) < 0.95f) {
        tool.spec.lean = want_lean;
        tool.sharpen();
      }
    }
    for (const std::vector<Pose>& motion : plan.motions) {
      maintain(tool, plan.stick);
      tool.stroke(cv_, motion, &changed);
    }
    return static_cast<int>(plan.motions.size());
  }

  Studio& st_;
  Canvas& cv_;
  float mm_;
  std::vector<ToolKind> kinds_;
  std::vector<Stick> sticks_;
  std::map<std::pair<int, int>, std::unique_ptr<DryTool>> tools_;  // by (stick, piece)
  Image target_;
  FlowField flow_;
  std::string note_range_;
  bool by_item_ = false;
  float overrun_px_ = 0.f;
  Outlines outlines_;  // when working by item: where the items' outlines run
};

// The box of colored sticks: pure hues, tints, pale tints and shades, plus
// neutrals and flesh tones. Stick palette order: white 0, yellow 1, red 2,
// crimson 3, ultramarine 4, phthalo green 5, umber 6, black 7.
std::vector<std::pair<std::string, PigVec>> color_box() {
  std::vector<std::pair<std::string, PigVec>> box;
  struct Hue { const char* name; PigVec p; };
  std::vector<Hue> hues = {
      {"yellow", mix({{1, 1.f}})},
      {"orange", mix({{1, 1.f}, {2, 0.45f}})},
      {"red", mix({{2, 1.f}})},
      {"crimson", mix({{3, 1.f}, {2, 0.3f}})},
      {"magenta", mix({{3, 1.f}})},
      {"violet", mix({{3, 0.5f}, {4, 1.f}})},
      {"blue", mix({{4, 1.f}})},
      {"sky blue", mix({{4, 1.f}, {5, 0.12f}})},
      {"teal", mix({{5, 0.5f}, {4, 0.5f}})},
      {"green", mix({{5, 0.3f}, {1, 1.f}})},
      {"leaf green", mix({{5, 0.07f}, {1, 1.f}})},
      {"olive", mix({{1, 1.f}, {7, 0.12f}, {5, 0.04f}})},
      {"ochre", mix({{1, 1.f}, {6, 0.6f}})},
      {"brown", mix({{6, 1.f}})},
      {"red brown", mix({{6, 1.f}, {2, 0.5f}})},
  };
  for (const Hue& h : hues) {
    auto with = [&](int extra, float amount) {
      PigVec p = h.p;
      float s = sum(p);
      for (float& v : p) v /= s;
      p[extra] += amount;
      return p;
    };
    box.push_back({h.name, with(0, 0.15f)});
    box.push_back({std::string("light ") + h.name, with(0, 2.5f)});
    box.push_back({std::string("pale ") + h.name, with(0, 9.f)});
    box.push_back({std::string("dark ") + h.name, with(7, 0.25f)});
  }
  box.push_back({"white", mix({{0, 1.f}})});
  box.push_back({"light gray", mix({{0, 1.f}, {7, 0.04f}})});
  box.push_back({"gray", mix({{0, 1.f}, {7, 0.16f}})});
  box.push_back({"dark gray", mix({{0, 1.f}, {7, 0.7f}})});
  box.push_back({"black", mix({{7, 1.f}})});
  box.push_back({"warm gray", mix({{0, 1.f}, {6, 0.25f}})});
  box.push_back({"light flesh", mix({{0, 1.f}, {2, 0.05f}, {1, 0.07f}})});
  box.push_back({"flesh", mix({{0, 1.f}, {2, 0.11f}, {1, 0.13f}, {6, 0.05f}})});
  box.push_back({"tan", mix({{0, 1.f}, {2, 0.18f}, {1, 0.2f}, {6, 0.22f}})});
  box.push_back({"dark flesh", mix({{6, 1.f}, {2, 0.4f}, {0, 0.5f}})});
  box.push_back({"blue gray", mix({{0, 1.f}, {4, 0.25f}, {7, 0.08f}})});
  box.push_back({"dark blue", mix({{4, 1.f}, {7, 0.5f}})});
  return box;
}

void draw_color(Studio& st, const std::string& medium) {
  Draftsman d(st);
  float effort = st.settings.effort;
  if (medium == "colored_pencil") {
    int k = d.add_kind(colored_pencil_spec(mix({{0, 1.f}})), 0.5f, 0.12f);
    for (auto& s : color_box()) d.add_stick(s.first, k, s.second);
    std::vector<int> all = d.all_sticks_of(k);
    d.prepare_target(1.f, 1.3f, 0.35f);
    d.work_by_item(0.35f);
    st.note(std::to_string(all.size()) + " colored pencils; " + d.range_note());
    d.hatch_pass({"colored pencil: first layer", all, 7.f, 0.55f, Dir::Fixed, -0.785f, 0.022f, 1}, 0.f, 0.2f);
    d.hatch_pass({"colored pencil: second layer", all, 5.5f, 0.5f, Dir::Along, -0.785f, 0.024f, 1}, 0.2f, 0.4f);
    d.hatch_pass({"colored pencil: third layer", all, 4.5f, 0.5f, Dir::Fixed, 0.6f, 0.028f, 1}, 0.4f, 0.58f);
    d.hatch_pass({"colored pencil: deepening", all, 3.5f, 0.45f, Dir::Across, -0.3f, 0.04f, effort >= 1.f ? 2 : 1}, 0.58f, 0.78f);
    if (effort >= 0.7f)
      d.hatch_pass({"colored pencil: details", all, 2.2f, 0.35f, Dir::Along, -0.785f, 0.045f, effort >= 1.3f ? 3 : 2}, 0.78f, 0.96f);
    // Soft accents: each strong edge is stated a little more firmly than
    // the subject has it, in the color of its own dark side.
    d.accents("colored pencil: accents", all, 0.96f, 1.f, 0.5f, 3.f, 0.13f);
  } else {  // pastel
    // A pastel stick is broken into short pieces (the side of a piece is
    // what lays the broad marks), so the artist has several of each color.
    const int pieces = 4;
    int k_side = d.add_kind(pastel_spec(mix({{0, 1.f}}), true), 9.f, 0.f, pieces);
    int k_tip = d.add_kind(pastel_spec(mix({{0, 1.f}}), false), 1.6f, 0.f, pieces);
    int k_pencil = d.add_kind(pastel_pencil_spec(mix({{0, 1.f}})), 0.6f, 0.4f);
    int k_finger = d.add_kind(stump_spec(13.f), 1.f, 0.f);
    for (auto& s : color_box()) {
      d.add_stick(s.first, k_side, s.second);
      d.add_stick(s.first, k_tip, s.second);
      d.add_stick(s.first, k_pencil, s.second);
    }
    int finger = d.add_stick("fingertip", k_finger, mix({{0, 1.f}}));
    std::vector<int> sides = d.all_sticks_of(k_side), tips = d.all_sticks_of(k_tip);
    std::vector<int> pencils = d.all_sticks_of(k_pencil);
    d.prepare_target(1.f, 1.f, 0.3f);
    d.work_by_item();
    st.note(std::to_string(sides.size()) + " pastels; " + d.range_note());
    d.hatch_pass({"pastel: lay-in", sides, 22.f, 9.f, Dir::Along, -0.785f, 0.024f, 1, 0.9f}, 0.f, 0.14f);
    d.blend("pastel: rubbing in", finger, 16.f, 0.14f, 0.22f, 0.6f);
    d.fix();
    d.hatch_pass({"pastel: masses", sides, 14.f, 8.f, Dir::Across, -0.3f, 0.028f, 1, 0.9f}, 0.22f, 0.4f);
    d.fix();
    d.hatch_pass({"pastel: modeling", tips, 7.f, 1.6f, Dir::Along, -0.785f, 0.04f, 2, 1.f}, 0.4f, 0.62f);
    d.fix();
    d.hatch_pass({"pastel: smaller forms", tips, 4.f, 1.2f, Dir::Across, -0.3f, 0.04f, 1, 1.f}, 0.62f, 0.76f);
    d.fix();
    if (effort >= 0.7f)
      d.hatch_pass({"pastel: details", pencils, 2.6f, 0.55f, Dir::Along, -0.785f, 0.045f, effort >= 1.3f ? 3 : 2, 1.f}, 0.76f, 0.97f);
    // Closing: the paper still shows in small spots where no stroke reached
    // into its grain, and they glare in the darks. The artist goes over the
    // whole sheet with the pastel pencils in very small patches, judging
    // each closely, and closes them. Worked by item, this also closes what
    // is left open between the marks of two items along their outline.
    if (effort >= 1.f) {
      DryPass closing{"pastel: closing", pencils, 1.6f, 0.4f, Dir::Along, -0.785f, 0.022f, 3, 1.f};
      closing.closing = true;
      d.hatch_pass(closing, 0.97f, 0.97f);
    }
    d.accents("pastel: accents", pencils, 0.97f, 1.f, 0.45f, 4.f);
  }
}

}  // namespace

void draw_dry(Studio& st, const std::string& medium) { draw_color(st, medium); }

}  // namespace bs
