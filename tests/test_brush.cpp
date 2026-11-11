// Bristle brush: paint is conserved, loads run out, pressure widens the mark,
// and a brush dragged through wet paint picks it up.
#include "check.hpp"
#include "media/bristle_brush.hpp"

using namespace bs;

namespace {
const float kMm = 0.25f;

std::vector<Vec2> line(Vec2 a, Vec2 b, int n = 20) {
  std::vector<Vec2> p;
  for (int i = 0; i <= n; ++i) p.push_back(lerp(a, b, static_cast<float>(i) / n));
  return p;
}
double canvas_paint_mm3(const Canvas& cv) {
  double t = 0.0;
  for (float v : cv.vol.v) t += v;
  return t * cv.mm_per_px * cv.mm_per_px;
}
double canvas_pigment_mm3(const Canvas& cv, int i) {
  double t = 0.0;
  for (const PigVec& p : cv.pig.v) t += p[i];
  return t * cv.mm_per_px * cv.mm_per_px;
}
int covered(const Canvas& cv, float min_film = 0.005f) {
  int n = 0;
  for (float v : cv.vol.v) n += v > min_film;
  return n;
}
BrushSpec flat10() {
  BrushSpec b;
  b.width_mm = 10.f; b.thick_mm = 2.5f; b.length_mm = 14.f; b.max_depth_mm = 3.5f;
  return b;
}
void stroke(Canvas& cv, BristleBrush& br, Vec2 a, Vec2 b, float press, uint64_t seed = 5) {
  Rng rng(seed);
  Gesture g;
  g.path = line(a, b);
  g.press = press;
  br.stroke(cv, perform(g, HandParams{}, kMm, rng));
}
}  // namespace

TEST(paint_is_conserved_between_brush_and_canvas) {
  Canvas cv(make_surface("canvas", 400, 200, kMm, 1), oil_palette());
  BristleBrush br(flat10(), kMm, 1);
  PigVec blue{};
  blue[5] = 1.f;
  br.load(blue, 80.f);
  double loaded = br.carried_mm3();
  CHECK_NEAR(loaded, 80.0, 0.01);
  stroke(cv, br, {60, 60}, {340, 60}, 0.6f);
  stroke(cv, br, {60, 120}, {340, 140}, 0.7f);
  double on_canvas = canvas_paint_mm3(cv);
  CHECK(on_canvas > 5.0);
  CHECK_NEAR(on_canvas + br.carried_mm3(), loaded, 0.02 * loaded);
  // Only the loaded pigment is on the canvas, and no more of it than the paint.
  CHECK(canvas_pigment_mm3(cv, 5) > 0.9 * on_canvas);
  CHECK(canvas_pigment_mm3(cv, 5) <= on_canvas * 1.001);
  CHECK(canvas_pigment_mm3(cv, 1) == 0.0);
}

TEST(a_small_load_runs_dry_along_the_stroke) {
  Canvas cv(make_surface("canvas", 600, 120, kMm, 2), oil_palette());
  BristleBrush br(flat10(), kMm, 2);
  PigVec red{};
  red[3] = 1.f;
  br.load(red, 9.f);
  stroke(cv, br, {40, 60}, {560, 60}, 0.6f);
  double first = 0.0, last = 0.0;
  for (int y = 0; y < cv.h; ++y)
    for (int x = 0; x < cv.w; ++x) (x < 220 ? first : (x >= 380 ? last : first)) += x < 220 || x >= 380 ? cv.vol(x, y) : 0.f;
  CHECK(first > 3.0 * last);
}

TEST(pressure_widens_the_mark) {
  PigVec dark{};
  dark[7] = 1.f;
  int cells[2];
  int idx = 0;
  for (float press : {0.25f, 0.85f}) {
    Canvas cv(make_surface("panel", 400, 160, kMm, 3), oil_palette());
    BristleBrush br(flat10(), kMm, 3);
    br.load(dark, 120.f);
    stroke(cv, br, {60, 80}, {340, 80}, press);
    cells[idx++] = covered(cv);
  }
  CHECK(cells[0] > 200);
  CHECK(cells[1] > cells[0] * 1.15);
}

TEST(brush_picks_up_wet_paint_it_crosses) {
  Canvas cv(make_surface("panel", 300, 300, kMm, 4), oil_palette());
  BristleBrush wide(flat10(), kMm, 4), narrow(flat10(), kMm, 5);
  PigVec yellow{}, blue{};
  yellow[1] = 1.f;
  blue[5] = 1.f;
  for (int i = 0; i < 4; ++i) {
    wide.load(yellow, 60.f);
    stroke(cv, wide, {40, 80.f + 35.f * i}, {260, 80.f + 35.f * i}, 0.65f, 10 + i);
  }
  double yellow_before = canvas_pigment_mm3(cv, 1);
  narrow.load(blue, 30.f);
  stroke(cv, narrow, {150, 40}, {150, 260}, 0.65f);
  // Some yellow left the canvas (it is in the brush now), and the blue stroke is no longer pure blue.
  CHECK(canvas_pigment_mm3(cv, 1) < yellow_before * 0.995);
  const PigVec& mid = cv.pig(150, 150);
  CHECK(mid[5] > 0.f && mid[1] > 0.f);
}

TEST(dry_brush_catches_the_weave_crowns_first) {
  // A nearly empty brush at light pressure: paint lands on high cells.
  Canvas cv(make_surface("canvas", 500, 140, kMm, 6), oil_palette());
  BristleBrush br(flat10(), kMm, 6);
  PigVec dark{};
  dark[7] = 1.f;
  br.load(dark, 3.f);
  stroke(cv, br, {40, 70}, {460, 70}, 0.3f);
  double tooth_painted = 0.0, tooth_bare = 0.0;
  int n_painted = 0, n_bare = 0;
  for (int y = 55; y < 85; ++y)
    for (int x = 120; x < 440; ++x) {
      if (cv.vol(x, y) > 0.0008f) { tooth_painted += cv.tooth(x, y); ++n_painted; }
      else { tooth_bare += cv.tooth(x, y); ++n_bare; }
    }
  std::printf("    %d cells painted, %d bare in the dry stretch\n", n_painted, n_bare);
  CHECK(n_painted > 50 && n_bare > 50);
  if (n_painted > 0 && n_bare > 0) CHECK(tooth_painted / n_painted > tooth_bare / n_bare);
}
