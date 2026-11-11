// Dry media: what the tip loses the paper gains, softer grades and harder
// pressure lay down more, light marks sit on the grain, and the stump and
// eraser move and remove material without creating any.
#include "check.hpp"
#include "media/dry_tool.hpp"

using namespace bs;

namespace {
const float kMm = 0.2f;

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
double line_deposit(const std::string& grade, float press) {
  Canvas cv(make_surface("drawing", 300, 80, kMm, 1), graphite_palette());
  DryTool t(pencil_spec(grade), kMm);
  draw(cv, t, {30, 40}, {270, 40}, press);
  return film_mm3(cv);
}
}  // namespace

TEST(tip_wear_equals_material_on_the_paper) {
  Canvas cv(make_surface("drawing", 300, 120, kMm, 1), graphite_palette());
  DryTool t(pencil_spec("2b"), kMm);
  for (int i = 0; i < 5; ++i) draw(cv, t, {30, 20.f + 18.f * i}, {270, 24.f + 18.f * i}, 0.7f, 3 + i);
  double on_paper = film_mm3(cv);
  CHECK(on_paper > 1e-5);
  CHECK_NEAR(t.deposited_mm3(), on_paper, 0.01 * on_paper);
  CHECK_NEAR(t.worn_mm3(), on_paper, 0.02 * on_paper);
}

TEST(softer_grade_and_harder_pressure_deposit_more) {
  double h2 = line_deposit("2h", 0.6f), b2 = line_deposit("2b", 0.6f), b6 = line_deposit("6b", 0.6f);
  CHECK(b2 > 1.5 * h2);
  CHECK(b6 > 1.3 * b2);
  CHECK(line_deposit("2b", 0.9f) > 1.3 * line_deposit("2b", 0.3f));
}

TEST(a_worn_point_draws_a_broader_line) {
  auto width_cells = [](bool pre_worn) {
    Canvas scrap(make_surface("bristol", 500, 200, kMm, 2), graphite_palette());
    DryTool t(pencil_spec("6b"), kMm);
    if (pre_worn)
      for (int i = 0; i < 40; ++i) draw(scrap, t, {30, 150}, {470, 150}, 0.8f, 20 + i);
    Canvas cv(make_surface("bristol", 300, 80, kMm, 2), graphite_palette());
    draw(cv, t, {30, 40}, {270, 40}, 0.6f);
    int cells = 0;
    for (int y = 0; y < cv.h; ++y) cells += cv.vol(150, y) > 1e-5f;
    return cells;
  };
  CHECK(width_cells(true) > width_cells(false));
}

TEST(light_marks_sit_on_the_grain) {
  Canvas cv(make_surface("cold_press", 320, 120, kMm, 3), stick_palette());
  PigVec red{};
  red[2] = 1.f;
  DryTool crayon(crayon_spec(red), kMm);
  draw(cv, crayon, {30, 60}, {290, 60}, 0.25f);
  // Compare cells that took wax with their bare neighbors inside the stroke's track.
  double tooth_marked = 0.0, tooth_bare = 0.0;
  int nm = 0, nb = 0;
  for (int y = 54; y < 66; ++y)
    for (int x = 60; x < 260; ++x) {
      if (cv.vol(x, y) > 0.002f) { tooth_marked += cv.tooth(x, y); ++nm; }
      else { tooth_bare += cv.tooth(x, y); ++nb; }
    }
  CHECK(nm > 30 && nb > 30);
  if (nm > 0 && nb > 0) CHECK(tooth_marked / nm > tooth_bare / nb);
}

TEST(stump_moves_material_and_eraser_removes_it) {
  Canvas cv(make_surface("drawing", 300, 160, kMm, 4), charcoal_palette());
  DryTool vine(charcoal_spec(false), kMm);
  for (int i = 0; i < 6; ++i) draw(cv, vine, {40, 40.f + 14.f * i}, {260, 40.f + 14.f * i}, 0.6f, 5 + i);
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
