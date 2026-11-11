// Drawing with dry media: graphite pencil, charcoal, colored pencil, wax
// crayon and soft pastel.
//
// The draftsman works the way one is taught to: find the contours, lay tone
// in broad hatched patches, then build it up in layers of smaller patches at
// changing angles, judging each patch against the subject before touching
// it. For each patch the artist picks, from the sticks in the box, the one
// whose mark over what is already there would come closest to the subject
// (predicted from test swatches made on a scrap of the same paper), and
// presses harder where more is needed. Hatch lines stop where the canvas
// no longer needs them, so the hatching takes the shape of the forms.
#include <map>
#include <memory>

#include "artist/paths.hpp"
#include "artist/studio.hpp"
#include "media/dry_tool.hpp"

namespace bs {
namespace {

const int kLevels = 4;
const float kEfforts[kLevels] = {0.12f, 0.3f, 0.6f, 0.9f};

// One physical kind of tool (shape, hardness), whatever its color.
struct ToolKind {
  DrySpec proto;
  float cal_spacing_mm = 0.5f;  // hatch spacing of the test swatch
  float film[kLevels] = {};     // mean deposit thickness (mm) of that swatch at kEfforts
  float sharpen_mm3 = 0.f;      // re-point after this much wear; 0 = never
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
};

PigVec mix(std::initializer_list<std::pair<int, float>> parts) {
  PigVec r{};
  for (auto& p : parts) r[p.first] = p.second;
  return r;
}

class Draftsman {
 public:
  Draftsman(Studio& st) : st_(st), cv_(st.canvas), mm_(st.canvas.mm_per_px) {}

  int add_kind(const DrySpec& proto, float cal_spacing_mm, float sharpen_mm3) {
    ToolKind k;
    k.proto = proto;
    k.cal_spacing_mm = cal_spacing_mm;
    k.sharpen_mm3 = sharpen_mm3;
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
  // mono: judge lightness only (graphite, charcoal). key > 1 keys the drawing
  // lighter: mid-tones open up toward the paper while the darks stay anchored,
  // as a draftsman reserves the paper for everything but the shadows.
  void prepare_target(bool mono, float chroma, float key, float emphasis) {
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
    // Percentile range of the subject's lightness.
    std::vector<float> ls;
    size_t stride = std::max<size_t>(1, st_.subject.light.v.size() / 50000);
    for (size_t i = 0; i < st_.subject.light.v.size(); i += stride) ls.push_back(st_.subject.light.v[i]);
    std::sort(ls.begin(), ls.end());
    float lo = ls[ls.size() * 2 / 100], hi = ls[ls.size() * 98 / 100];
    float span = std::max(hi - lo, 0.05f);
    // A drawing states local contrast more firmly than a photograph does:
    // each point is judged against its surroundings (a few mm), which is
    // what makes features read once the overall key has been lightened.
    Plane<float> surround = st_.subject.light;
    gaussian_blur(surround, std::max(2.f, 2.2f / mm_));
    target_ = Image(cv_.w, cv_.h);
    parallel_for(0, cv_.h, [&](int y0, int y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < cv_.w; ++x) {
          Lab s = rgb_to_oklab(st_.subject.color(x, y));
          float u = clamp01((s.L - lo) / span);
          u = 1.f - std::pow(1.f - u, key);
          u = clamp01(u + emphasis * (s.L - surround(x, y)) / span);
          Lab t;
          t.L = l_dark + (l_top - l_dark) * u;
          if (mono) { t.a = paper.a; t.b = paper.b; }
          else { t.a = s.a * chroma; t.b = s.b * chroma; }
          target_(x, y) = oklab_to_rgb(t);
        }
    }, 8);
    gaussian_blur(target_, std::max(0.7f, 0.25f / mm_));
    flow_ = compute_flow(st_.subject.color, std::max(2.f, 1.6f / mm_));
    note_range_ = "tonal range L " + std::to_string(l_dark).substr(0, 4) + " to " + std::to_string(l_top).substr(0, 4);
  }

  // Contour drawing: lines along the strongest edges of the subject.
  void contour(const std::string& name, int stick, float lo, float hi, float min_edge, float cell_mm,
               float effort_lo, float effort_hi) {
    st_.begin_stage(name, lo, hi);
    int before = st_.strokes;
    DryTool& tool = tool_for(stick);
    const int g = std::max(4, static_cast<int>(cell_mm / mm_));
    Plane<uint8_t> drawn((cv_.w + 3) / 4, (cv_.h + 3) / 4, 0);
    struct Seed { Vec2 p; float e; };
    std::vector<Seed> seeds;
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
    std::sort(seeds.begin(), seeds.end(), [](const Seed& a, const Seed& b) { return a.e > b.e; });
    HandParams hand = tool.hand();
    hand.cruise_mm_s = 90.f;
    for (size_t i = 0; i < seeds.size() && !st_.cancelled; ++i) {
      Vec2 p = seeds[i].p;
      if (drawn(static_cast<int>(p.x) / 4, static_cast<int>(p.y) / 4)) continue;
      float e0 = seeds[i].e;
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
      if (path_length(path) * mm_ < 2.f) continue;
      for (const Vec2& q : path) {
        int cx = static_cast<int>(q.x) / 4, cy = static_cast<int>(q.y) / 4;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
            if (drawn.inside(cx + dx, cy + dy)) drawn(cx + dx, cy + dy) = 1;
      }
      Gesture gst;
      gst.path = smooth_path(path, 2);
      // Press according to how dark the line needs to be along its length.
      gst.pressure.resize(gst.path.size());
      for (size_t k = 0; k < gst.path.size(); ++k) {
        int x = std::clamp(static_cast<int>(gst.path[k].x), 0, cv_.w - 1);
        int y = std::clamp(static_cast<int>(gst.path[k].y), 0, cv_.h - 1);
        float dark = 1.f - rgb_to_oklab(target_(x, y)).L;
        gst.pressure[k] = lerp(effort_lo, effort_hi, clamp01(0.5f * st_.subject.edge(x, y) + 0.7f * dark));
      }
      maintain(tool, stick);
      tool.stroke(cv_, perform(gst, hand, mm_, st_.rng));
      ++st_.strokes;
      st_.report(static_cast<float>(i) / seeds.size());
    }
    st_.look();
    st_.note(name + ": " + std::to_string(st_.strokes - before) + " lines, " + sticks_[stick].name);
  }

  // Tonal / color build-up by hatched patches.
  void hatch_pass(const DryPass& pass, float lo, float hi) {
    st_.begin_stage(pass.name, lo, hi);
    int before = st_.strokes;
    const float patch = pass.patch_mm / mm_;
    const float spacing = pass.spacing_mm / mm_;
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

      for (size_t ci = 0; ci < centers.size() && !st_.cancelled; ++ci) {
        Vec2 c = centers[ci];
        int stick = -1, eidx = 0;
        Lab delta;
        if (!decide(c, patch, pass, stick, eidx, delta)) continue;
        Vec2 d = direction(c, pass, round);
        hatch_patch(c, patch, spacing, d, stick, kEfforts[eidx], delta, pass.tempo);
        st_.look();
        st_.report((round + static_cast<float>(ci) / centers.size()) / pass.rounds);
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
        tool.stroke(cv_, perform(g, hand, mm_, st_.rng));
        ++st_.strokes;
        st_.report(static_cast<float>(j * nx + i) / (nx * ny));
      }
    st_.look();
    st_.note(name + ": " + std::to_string(st_.strokes - before) + " strokes");
  }

  // Lifts material where the drawing has become darker than the subject.
  void lift(const std::string& name, int stick, float patch_mm, float lo, float hi, float margin) {
    st_.begin_stage(name, lo, hi);
    int before = st_.strokes;
    DryTool& tool = tool_for(stick);
    HandParams hand = tool.hand();
    const float patch = patch_mm / mm_;
    st_.look();
    for (float y = 0.5f * patch; y < cv_.h && !st_.cancelled; y += patch)
      for (float x = 0.5f * patch; x < cv_.w && !st_.cancelled; x += patch) {
        Vec2 c{x, y};
        float want = rgb_to_oklab(st_.glance(target_, c, patch * 0.3f)).L;
        float have = rgb_to_oklab(st_.glance(st_.seen, c, patch * 0.3f)).L;
        if (have > want - margin) continue;
        Vec2 d = direction(c, DryPass{"", {}, patch_mm, 1.f, Dir::Along, -0.785f}, 0);
        float sp = std::max(2.f, 0.4f * tool.spec.core_mm / mm_);
        int n = std::max(1, static_cast<int>(patch / sp));
        for (int i = 0; i < n; ++i) {
          Vec2 m = c + perp(d) * ((i - 0.5f * (n - 1)) * sp);
          Gesture g;
          g.path = arc_path(m - d * (0.55f * patch), m + d * (0.55f * patch), 0.02f);
          g.pressure.resize(g.path.size());
          bool any = false;
          for (size_t k = 0; k < g.path.size(); ++k) {
            float w2 = rgb_to_oklab(st_.glance(target_, g.path[k], 2.f)).L;
            float h2 = rgb_to_oklab(st_.glance(st_.seen, g.path[k], 2.f)).L;
            g.pressure[k] = clamp01((w2 - h2 - 0.5f * margin) * 6.f);
            any = any || g.pressure[k] > 0.05f;
          }
          if (!any) continue;
          tool.stroke(cv_, perform(g, hand, mm_, st_.rng));
          ++st_.strokes;
        }
        st_.look();
        st_.report(y / cv_.h);
      }
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
  // The stick whose solid mark on this paper is darkest.
  int darkest_stick(int kind) const {
    int best = -1;
    float bl = 9.f;
    for (size_t i = 0; i < sticks_.size(); ++i) {
      if (sticks_[i].kind != kind) continue;
      float L = rgb_to_oklab(spectrum_to_rgb(cv_.palette.masstone(sticks_[i].recipe))).L;
      if (L < bl) { bl = L; best = static_cast<int>(i); }
    }
    return best;
  }
  const std::string& range_note() const { return note_range_; }

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

  DryTool& tool_for(int stick) {
    auto it = tools_.find(stick);
    if (it != tools_.end()) return *it->second;
    DrySpec spec = kinds_[sticks_[stick].kind].proto;
    spec.recipe = sticks_[stick].recipe;
    auto t = std::make_unique<DryTool>(spec, mm_);
    DryTool& ref = *t;
    tools_[stick] = std::move(t);
    return ref;
  }

  void maintain(DryTool& tool, int stick) {
    float lim = kinds_[sticks_[stick].kind].sharpen_mm3;
    if (lim > 0.f && tool.worn_mm3() > lim) tool.sharpen();
  }

  Vec2 direction(Vec2 c, const DryPass& pass, int round) {
    float ang = pass.angle + 0.5f * round + st_.rng.normal() * 0.12f;
    Vec2 fixed{std::cos(ang), std::sin(ang)};
    if (pass.dir == Dir::Fixed) return fixed;
    int x = std::clamp(static_cast<int>(c.x), 0, cv_.w - 1), y = std::clamp(static_cast<int>(c.y), 0, cv_.h - 1);
    if (flow_.coherence(x, y) < 0.2f || flow_.strength(x, y) < 0.1f) return fixed;
    Vec2 d = flow_.dir(x, y);
    if (pass.dir == Dir::Across) d = perp(d);
    d = rotate(d, st_.rng.normal() * 0.1f + 0.35f * round);
    return d;
  }

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

  // Which stick, and how hard? Returns false if the patch needs nothing.
  bool decide(Vec2 c, float patch, const DryPass& pass, int& stick, int& eidx, Lab& delta) {
    Lab want = rgb_to_oklab(st_.glance(target_, c, patch * 0.3f));
    Lab have = rgb_to_oklab(st_.glance(st_.seen, c, patch * 0.3f));
    float err = lab_dist(want, have);
    float detail = st_.subject.detail(std::clamp(static_cast<int>(c.x), 0, cv_.w - 1),
                                      std::clamp(static_cast<int>(c.y), 0, cv_.h - 1));
    if (err < pass.threshold / (1.f + 0.8f * detail)) return false;
    // A patch is only worked if its neighborhood agrees that something is
    // missing; otherwise borderline areas break up into isolated blocks.
    float err_wide = lab_dist(rgb_to_oklab(st_.glance(target_, c, patch * 1.1f)),
                              rgb_to_oklab(st_.glance(st_.seen, c, patch * 1.1f)));
    if (err_wide < 0.6f * pass.threshold / (1.f + 0.8f * detail)) return false;
    Spectrum base = mean_reflectance(c, patch * 0.3f);
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

  void hatch_patch(Vec2 c, float patch, float spacing, Vec2 d, int stick, float effort, Lab delta, float tempo) {
    DryTool& tool = tool_for(stick);
    if (tool.spec.tip == DrySpec::Tip::Bar) {
      // A stick used on its side is held across the direction of the stroke.
      Vec2 want_lean = perp(d);
      if (std::abs(dot(tool.spec.lean, want_lean)) < 0.95f) {
        tool.spec.lean = want_lean;
        tool.sharpen();
      }
    }
    HandParams hand = tool.hand();
    const float d2 = std::max(sqr(delta.L) + sqr(delta.a) + sqr(delta.b), 1e-8f);
    const float min_run = std::max(3.f, 1.2f / mm_);
    const float sample = 2.f;
    Vec2 across = perp(d);
    int n = std::max(1, static_cast<int>(std::ceil(patch * 1.05f / spacing)));
    for (int i = 0; i < n; ++i) {
      float off = (i - 0.5f * (n - 1)) * spacing + st_.rng.normal() * 0.18f * spacing;
      float half = 0.6f * patch * (1.f + 0.12f * st_.rng.normal());
      Vec2 a = c + across * off - d * half, b = c + across * off + d * half;
      if (i % 2 == 1) std::swap(a, b);  // the hand goes back and forth
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
          if (p.x >= 0.f && p.y >= 0.f && p.x < cv_.w && p.y < cv_.h) {
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
            g.press = clamp01(effort * std::clamp(mean, 0.45f, 1.5f) * st_.rng.range(0.9f, 1.1f));
            g.tempo = tempo;
            maintain(tool, stick);
            tool.stroke(cv_, perform(g, hand, mm_, st_.rng));
            ++st_.strokes;
          }
          run_start = -1;
        }
      }
    }
  }

  Studio& st_;
  Canvas& cv_;
  float mm_;
  std::vector<ToolKind> kinds_;
  std::vector<Stick> sticks_;
  std::map<int, std::unique_ptr<DryTool>> tools_;
  Image target_;
  FlowField flow_;
  std::string note_range_;
};

// The box of colored sticks: pure hues, tints, pale tints and shades, plus
// neutrals and flesh tones. Stick palette order: white 0, yellow 1, red 2,
// crimson 3, ultramarine 4, phthalo green 5, umber 6, black 7.
std::vector<std::pair<std::string, PigVec>> color_box(bool small_box) {
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
    if (!small_box) {
      box.push_back({std::string("pale ") + h.name, with(0, 9.f)});
      box.push_back({std::string("dark ") + h.name, with(7, 0.25f)});
    }
  }
  box.push_back({"white", mix({{0, 1.f}})});
  box.push_back({"light gray", mix({{0, 1.f}, {7, 0.04f}})});
  box.push_back({"gray", mix({{0, 1.f}, {7, 0.16f}})});
  box.push_back({"dark gray", mix({{0, 1.f}, {7, 0.7f}})});
  box.push_back({"black", mix({{7, 1.f}})});
  box.push_back({"warm gray", mix({{0, 1.f}, {6, 0.25f}})});
  box.push_back({"light flesh", mix({{0, 1.f}, {2, 0.05f}, {1, 0.07f}})});
  box.push_back({"flesh", mix({{0, 1.f}, {2, 0.11f}, {1, 0.13f}, {6, 0.05f}})});
  if (!small_box) {
    box.push_back({"tan", mix({{0, 1.f}, {2, 0.18f}, {1, 0.2f}, {6, 0.22f}})});
    box.push_back({"dark flesh", mix({{6, 1.f}, {2, 0.4f}, {0, 0.5f}})});
    box.push_back({"blue gray", mix({{0, 1.f}, {4, 0.25f}, {7, 0.08f}})});
    box.push_back({"dark blue", mix({{4, 1.f}, {7, 0.5f}})});
  }
  return box;
}

void draw_pencil(Studio& st) {
  Draftsman d(st);
  float effort = st.settings.effort;
  int k_hb = d.add_kind(pencil_spec("hb"), 0.5f, 0.05f);
  int k_2b = d.add_kind(pencil_spec("2b"), 0.5f, 0.07f);
  int k_6b = d.add_kind(pencil_spec("6b"), 0.5f, 0.10f);
  DrySpec side = pencil_spec("6b");
  side.name = "pencil 6b (side of the lead)";
  side.tilt = 1.3f;
  int k_side = d.add_kind(side, 1.0f, 0.f);
  PigVec g = mix({{0, 1.f}});
  int hb = d.add_stick("pencil HB", k_hb, g), b2 = d.add_stick("pencil 2B", k_2b, g);
  int b6 = d.add_stick("pencil 6B", k_6b, g), sd = d.add_stick("pencil 6B, side", k_side, g);
  d.prepare_target(true, 0.f, 1.5f, 0.8f);
  st.note(d.range_note());

  d.contour("pencil: contours", hb, 0.00f, 0.06f, 0.14f, 1.8f, 0.3f, 0.7f);
  d.hatch_pass({"pencil: light tone", {hb}, 8.f, 0.6f, Dir::Fixed, -0.785f, 0.035f, 1}, 0.06f, 0.2f);
  d.hatch_pass({"pencil: following the form", {hb, b2}, 6.f, 0.5f, Dir::Along, -0.785f, 0.035f, 1}, 0.2f, 0.36f);
  d.hatch_pass({"pencil: cross-hatching", {b2, b6}, 5.f, 0.5f, Dir::Fixed, 0.6f, 0.04f, 1}, 0.36f, 0.52f);
  d.hatch_pass({"pencil: darks", {b6, sd}, 4.f, 0.45f, Dir::Across, -0.3f, 0.04f, effort >= 1.f ? 2 : 1}, 0.52f, 0.72f);
  if (effort >= 0.7f)
    d.hatch_pass({"pencil: details", {b2, b6}, 2.2f, 0.32f, Dir::Along, -0.785f, 0.045f, effort >= 1.3f ? 3 : 2}, 0.72f, 0.95f);
  d.contour("pencil: accents", b2, 0.95f, 1.f, 0.3f, 2.f, 0.4f, 0.9f);
}

void draw_charcoal(Studio& st) {
  Draftsman d(st);
  float effort = st.settings.effort;
  int k_vine = d.add_kind(charcoal_spec(false), 3.0f, 0.f);
  int k_comp = d.add_kind(charcoal_spec(true), 0.9f, 0.5f);
  int k_stump = d.add_kind(stump_spec(9.f), 1.f, 0.f);
  int k_eraser = d.add_kind(eraser_spec(4.f), 1.f, 0.f);
  PigVec c = mix({{0, 1.f}});
  int vine = d.add_stick("vine charcoal", k_vine, c), comp = d.add_stick("compressed charcoal", k_comp, c);
  int stump = d.add_stick("stump", k_stump, c), eraser = d.add_stick("kneaded eraser", k_eraser, c);
  d.prepare_target(true, 0.f, 1.2f, 0.2f);
  st.note(d.range_note());

  d.hatch_pass({"charcoal: block-in", {vine}, 18.f, 3.5f, Dir::Fixed, -0.785f, 0.025f, 1, 1.f}, 0.f, 0.14f);
  d.blend("charcoal: smudging", stump, 14.f, 0.14f, 0.24f, 0.65f);
  d.hatch_pass({"charcoal: tone", {vine}, 9.f, 3.f, Dir::Along, -0.785f, 0.025f, 2, 1.f}, 0.24f, 0.4f);
  d.contour("charcoal: drawing", comp, 0.4f, 0.48f, 0.25f, 4.f, 0.25f, 0.6f);
  d.hatch_pass({"charcoal: darks", {comp}, 7.f, 0.8f, Dir::Fixed, -0.785f, 0.04f, 2}, 0.48f, 0.68f);
  (void)eraser;  // lifting lights is available (see the dry study) but not yet part of this routine
  if (effort >= 0.7f)
    d.hatch_pass({"charcoal: details", {comp}, 3.5f, 0.5f, Dir::Along, -0.785f, 0.045f, effort >= 1.3f ? 3 : 2}, 0.78f, 1.f);
}

void draw_color(Studio& st, const std::string& medium) {
  Draftsman d(st);
  float effort = st.settings.effort;
  if (medium == "colored_pencil") {
    int k = d.add_kind(colored_pencil_spec(mix({{0, 1.f}})), 0.5f, 0.12f);
    for (auto& s : color_box(false)) d.add_stick(s.first, k, s.second);
    std::vector<int> all = d.all_sticks_of(k);
    d.prepare_target(false, 1.f, 1.3f, 0.35f);
    st.note(std::to_string(all.size()) + " colored pencils; " + d.range_note());
    d.hatch_pass({"colored pencil: first layer", all, 7.f, 0.55f, Dir::Fixed, -0.785f, 0.035f, 1}, 0.f, 0.2f);
    d.hatch_pass({"colored pencil: second layer", all, 5.5f, 0.5f, Dir::Along, -0.785f, 0.035f, 1}, 0.2f, 0.4f);
    d.hatch_pass({"colored pencil: third layer", all, 4.5f, 0.5f, Dir::Fixed, 0.6f, 0.04f, 1}, 0.4f, 0.58f);
    d.hatch_pass({"colored pencil: deepening", all, 3.5f, 0.45f, Dir::Across, -0.3f, 0.04f, effort >= 1.f ? 2 : 1}, 0.58f, 0.78f);
    if (effort >= 0.7f)
      d.hatch_pass({"colored pencil: details", all, 2.2f, 0.35f, Dir::Along, -0.785f, 0.045f, effort >= 1.3f ? 3 : 2}, 0.78f, 0.96f);
    d.contour("colored pencil: accents", d.darkest_stick(k), 0.96f, 1.f, 0.5f, 3.f, 0.25f, 0.6f);
  } else if (medium == "crayon") {
    int k = d.add_kind(crayon_spec(mix({{0, 1.f}})), 1.6f, 4.f);
    for (auto& s : color_box(true)) d.add_stick(s.first, k, s.second);
    std::vector<int> all = d.all_sticks_of(k);
    d.prepare_target(false, 1.05f, 1.2f, 0.3f);
    st.note(std::to_string(all.size()) + " crayons; " + d.range_note());
    d.hatch_pass({"crayon: filling in", all, 14.f, 1.8f, Dir::Fixed, -0.785f, 0.04f, 1, 1.1f}, 0.f, 0.22f);
    d.hatch_pass({"crayon: second coat", all, 9.f, 1.6f, Dir::Fixed, 0.6f, 0.04f, 1, 1.1f}, 0.22f, 0.45f);
    d.hatch_pass({"crayon: working it up", all, 6.f, 1.4f, Dir::Along, -0.785f, 0.04f, 2, 1.1f}, 0.45f, 0.75f);
    if (effort >= 0.7f)
      d.hatch_pass({"crayon: details", all, 3.5f, 1.1f, Dir::Along, -0.785f, 0.045f, effort >= 1.3f ? 3 : 2, 1.f}, 0.75f, 0.95f);
    d.contour("crayon: outlines", d.darkest_stick(k), 0.95f, 1.f, 0.45f, 4.f, 0.35f, 0.75f);
  } else {  // pastel
    int k_side = d.add_kind(pastel_spec(mix({{0, 1.f}}), true), 9.f, 0.f);
    int k_tip = d.add_kind(pastel_spec(mix({{0, 1.f}}), false), 1.6f, 0.f);
    int k_pencil = d.add_kind(pastel_pencil_spec(mix({{0, 1.f}})), 0.6f, 0.4f);
    int k_finger = d.add_kind(stump_spec(13.f), 1.f, 0.f);
    for (auto& s : color_box(false)) {
      d.add_stick(s.first, k_side, s.second);
      d.add_stick(s.first, k_tip, s.second);
      d.add_stick(s.first, k_pencil, s.second);
    }
    int finger = d.add_stick("fingertip", k_finger, mix({{0, 1.f}}));
    std::vector<int> sides = d.all_sticks_of(k_side), tips = d.all_sticks_of(k_tip);
    std::vector<int> pencils = d.all_sticks_of(k_pencil);
    d.prepare_target(false, 1.f, 1.f, 0.3f);
    st.note(std::to_string(sides.size()) + " pastels; " + d.range_note());
    d.hatch_pass({"pastel: lay-in", sides, 22.f, 9.f, Dir::Along, -0.785f, 0.04f, 1, 0.9f}, 0.f, 0.14f);
    d.blend("pastel: rubbing in", finger, 16.f, 0.14f, 0.22f, 0.6f);
    d.fix();
    d.hatch_pass({"pastel: masses", sides, 14.f, 8.f, Dir::Across, -0.3f, 0.04f, 1, 0.9f}, 0.22f, 0.4f);
    d.fix();
    d.hatch_pass({"pastel: modeling", tips, 7.f, 1.6f, Dir::Along, -0.785f, 0.04f, 2, 1.f}, 0.4f, 0.62f);
    d.fix();
    d.hatch_pass({"pastel: smaller forms", tips, 4.f, 1.2f, Dir::Across, -0.3f, 0.04f, 1, 1.f}, 0.62f, 0.76f);
    d.fix();
    if (effort >= 0.7f)
      d.hatch_pass({"pastel: details", pencils, 2.6f, 0.55f, Dir::Along, -0.785f, 0.045f, effort >= 1.3f ? 3 : 2, 1.f}, 0.76f, 0.97f);
    d.contour("pastel: accents", d.darkest_stick(k_pencil), 0.97f, 1.f, 0.45f, 4.f, 0.3f, 0.7f);
  }
}

}  // namespace

void draw_dry(Studio& st, const std::string& medium) {
  if (medium == "pencil") draw_pencil(st);
  else if (medium == "charcoal") draw_charcoal(st);
  else draw_color(st, medium);
}

}  // namespace bs
