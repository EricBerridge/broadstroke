// Dry media: what the tip loses the paper gains, softer sticks and harder
// pressure lay down more, light marks sit on the grain, and the stump and
// eraser move and remove material without creating any.
#include "check.hpp"
#include "media/dry_tool.hpp"

using namespace bs;

namespace {
const float kMm = 0.2f;

PigVec pure(int pigment) {
  PigVec p{};
  p[pigment] = 1.f;
  return p;
}
std::vector<Vec2> line(Vec2 a, Vec2 b, int n = 16) {
  std::vector<Vec2> p;
  for (int i = 0; i <= n; ++i) p.push_back(lerp(a, b, static_cast<float>(i) / n));
  return p;
}
double film_mm3(const Canvas& cv) {
  double t = 0.0;
  for (float v : cv.vol.v) t += v;
  return t * cv.mm_per_px * cv.mm_per_px;
}
void draw(Canvas& cv, DryTool& t, Vec2 a, Vec2 b, float press, uint64_t seed = 3) {
  Rng rng(seed);
  Gesture g;
  g.path = line(a, b);
  g.press = press;
  t.stroke(cv, perform(g, t.hand(), kMm, rng));
}
double line_deposit(const DrySpec& spec, float press) {
  Canvas cv(make_surface("drawing", 300, 80, kMm, 1), stick_palette());
  DryTool t(spec, kMm);
  draw(cv, t, {30, 40}, {270, 40}, press);
  return film_mm3(cv);
}
}  // namespace

TEST(tip_wear_equals_material_on_the_paper) {
  Canvas cv(make_surface("drawing", 300, 120, kMm, 1), stick_palette());
  DryTool t(colored_pencil_spec(pure(2)), kMm);
  for (int i = 0; i < 5; ++i) draw(cv, t, {30, 20.f + 18.f * i}, {270, 24.f + 18.f * i}, 0.7f, 3 + i);
  double on_paper = film_mm3(cv);
  CHECK(on_paper > 1e-5);
  CHECK_NEAR(t.deposited_mm3(), on_paper, 0.01 * on_paper);
  CHECK_NEAR(t.worn_mm3(), on_paper, 0.02 * on_paper);
}

TEST(softer_stick_and_harder_pressure_deposit_more) {
  // Colored pencil is the hardest, a pastel pencil softer, a pastel corner softer still.
  double cp = line_deposit(colored_pencil_spec(pure(2)), 0.6f);
  double pp = line_deposit(pastel_pencil_spec(pure(2)), 0.6f);
  double pc = line_deposit(pastel_spec(pure(2), false), 0.6f);
  CHECK(pp > 1.3 * cp);
  CHECK(pc > 1.3 * pp);
  CHECK(line_deposit(colored_pencil_spec(pure(2)), 0.9f) > 1.3 * line_deposit(colored_pencil_spec(pure(2)), 0.3f));
}

TEST(a_worn_point_draws_a_broader_line) {
  auto width_cells = [](bool pre_worn) {
    Canvas scrap(make_surface("bristol", 500, 200, kMm, 2), stick_palette());
    DryTool t(colored_pencil_spec(pure(7)), kMm);
    if (pre_worn)
      for (int i = 0; i < 40; ++i) draw(scrap, t, {30, 150}, {470, 150}, 0.8f, 20 + i);
    Canvas cv(make_surface("bristol", 300, 80, kMm, 2), stick_palette());
    draw(cv, t, {30, 40}, {270, 40}, 0.6f);
    int cells = 0;
    for (int y = 0; y < cv.h; ++y) cells += cv.vol(150, y) > 1e-5f;
    return cells;
  };
  CHECK(width_cells(true) > width_cells(false));
}

TEST(light_marks_sit_on_the_grain) {
  Canvas cv(make_surface("cold_press", 320, 120, kMm, 3), stick_palette());
  DryTool pencil(colored_pencil_spec(pure(2)), kMm);
  // A few light passes side by side, so the track is wide enough to sample.
  for (int i = 0; i < 6; ++i) draw(cv, pencil, {30, 55.f + 2.f * i}, {290, 55.f + 2.f * i}, 0.25f, 3 + i);
  // Compare cells that took pigment with their bare neighbors inside the track.
  double tooth_marked = 0.0, tooth_bare = 0.0;
  int nm = 0, nb = 0;
  for (int y = 54; y < 66; ++y)
    for (int x = 60; x < 260; ++x) {
      if (cv.vol(x, y) > 0.0005f) { tooth_marked += cv.tooth(x, y); ++nm; }
      else { tooth_bare += cv.tooth(x, y); ++nb; }
    }
  CHECK(nm > 30 && nb > 30);
  if (nm > 0 && nb > 0) CHECK(tooth_marked / nm > tooth_bare / nb);
}

TEST(stump_moves_material_and_eraser_removes_it) {
  Canvas cv(make_surface("pastel_paper", 300, 160, kMm, 4), stick_palette());
  DryTool pastel(pastel_spec(pure(4), true), kMm);
  for (int i = 0; i < 6; ++i) draw(cv, pastel, {40, 40.f + 14.f * i}, {260, 40.f + 14.f * i}, 0.6f, 5 + i);
  double laid = film_mm3(cv);
  CHECK(laid > 1e-4);
  DryTool stump(stump_spec(6.f), kMm);
  for (int i = 0; i < 8; ++i) draw(cv, stump, {40, 45.f + 10.f * i}, {260, 45.f + 10.f * i}, 0.7f, 30 + i);
  double smudged = film_mm3(cv);
  CHECK(smudged <= laid * 1.0001);  // a stump adds nothing
  CHECK(smudged > 0.5 * laid);      // and carries off only a little
  DryTool eraser(eraser_spec(5.f), kMm);
  float before = cv.vol(150, 80);
  for (int i = 0; i < 3; ++i) draw(cv, eraser, {60, 80}, {240, 80}, 0.9f, 50 + i);
  CHECK(film_mm3(cv) < smudged * 0.98);
  CHECK(cv.vol(150, 80) < 0.5f * before);
}
