// Surfaces: relief statistics, determinism, and the canvas that sits on them.
#include "check.hpp"
#include "canvas/canvas.hpp"

using namespace bs;

TEST(surfaces_have_centered_bounded_relief) {
  for (const std::string& name : surface_names()) {
    Surface s = make_surface(name, 160, 160, 0.2f, 3);
    CHECK(s.w == 160 && s.h == 160);
    double mean = 0.0;
    float lo = 1e9f, hi = -1e9f;
    for (float v : s.height.v) { mean += v; lo = std::min(lo, v); hi = std::max(hi, v); }
    mean /= s.height.v.size();
    CHECK(std::fabs(mean) < 0.15 * s.relief);
    CHECK(hi > 0.5f * s.relief && lo < -0.5f * s.relief);
    CHECK(hi < 4.f * s.relief && lo > -4.f * s.relief);
    Rgb c = spectrum_to_rgb(s.reflectance);
    CHECK(c.g > 0.2f && c.g <= 1.f);
  }
}

TEST(surfaces_are_deterministic) {
  Surface a = make_surface("cold_press", 96, 96, 0.25f, 9);
  Surface b = make_surface("cold_press", 96, 96, 0.25f, 9);
  Surface c = make_surface("cold_press", 96, 96, 0.25f, 10);
  bool same = true, differs = false;
  for (size_t i = 0; i < a.height.v.size(); ++i) {
    same = same && a.height.v[i] == b.height.v[i];
    differs = differs || a.height.v[i] != c.height.v[i];
  }
  CHECK(same);
  CHECK(differs);
}

TEST(rough_paper_is_rougher_than_bristol) {
  CHECK(make_surface("rough", 64, 64, 0.2f, 1).relief > 5.f * make_surface("bristol", 64, 64, 0.2f, 1).relief);
  CHECK(!is_surface_name("no_such_surface"));
  CHECK(make_surface("no_such_surface", 32, 32, 0.2f, 1).name == "drawing");
}

TEST(drying_bakes_film_into_base) {
  Canvas cv(make_surface("panel", 48, 48, 0.25f, 2), oil_palette());
  PigVec red{};
  red[3] = 0.08f;
  for (int y = 10; y < 30; ++y)
    for (int x = 10; x < 30; ++x) { cv.pig(x, y) = red; cv.vol(x, y) = 0.08f; }
  Rgb wet = cv.albedo(20, 20), bare = cv.albedo(2, 2);
  CHECK(wet.r > wet.g * 2.f);
  cv.dry();
  Rgb dried = cv.albedo(20, 20);
  CHECK_NEAR(dried.r, wet.r, 1e-4);
  CHECK_NEAR(dried.g, wet.g, 1e-4);
  CHECK(cv.vol(20, 20) == 0.f);
  CHECK_NEAR(cv.dry_h(20, 20), 0.08, 1e-6);
  CHECK_NEAR(cv.albedo(2, 2).g, bare.g, 1e-6);
}
