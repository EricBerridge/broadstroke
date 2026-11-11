// Watercolor: pigment is conserved, water only leaves, a wash on dry paper
// stays where it was put, and it dries with a darker rim.
#include "check.hpp"
#include "media/watercolor.hpp"

using namespace bs;

namespace {
const float kMm = 0.25f;

// Lays a disc of water carrying one pigment directly into the simulation.
void puddle(WatercolorSim& sim, int cx, int cy, int r, float depth, int pigment, float load) {
  for (int y = cy - r; y <= cy + r; ++y)
    for (int x = cx - r; x <= cx + r; ++x)
      if (sqr(static_cast<float>(x - cx)) + sqr(static_cast<float>(y - cy)) <= static_cast<float>(r * r)) {
        sim.water(x, y) += depth;
        if (pigment >= 0) sim.susp(x, y)[pigment] += load;
      }
  sim.touch({cx - r, cy - r, cx + r + 1, cy + r + 1});
}
}  // namespace

TEST(pigment_is_conserved_while_water_only_leaves) {
  Canvas cv(make_surface("cold_press", 200, 200, kMm, 1), watercolor_palette());
  cv.film_relief = 0.f;
  WatercolorSim sim(cv);
  puddle(sim, 100, 100, 40, 0.12f, 4, 0.6f);
  PigVec start = sim.total_pigment();
  double water = sim.total_water();
  CHECK(start[4] > 100.f);
  for (int i = 0; i < 6; ++i) {
    sim.advance(5.f);
    PigVec now = sim.total_pigment();
    CHECK_NEAR(now[4], start[4], 2e-3 * start[4]);
    double w = sim.total_water();
    CHECK(w <= water + 1e-3);
    water = w;
  }
  sim.dry();
  CHECK(!sim.any_wet());
  PigVec end = sim.total_pigment();
  CHECK_NEAR(end[4], start[4], 2e-3 * start[4]);
  // Everything has settled onto the paper.
  double suspended = 0.0;
  for (const PigVec& p : sim.susp.v) suspended += p[4];
  CHECK(suspended == 0.0);
  for (float w : sim.water.v) CHECK(w == 0.f);
}

TEST(a_wash_on_dry_paper_keeps_its_shape) {
  Canvas cv(make_surface("hot_press", 240, 240, kMm, 2), watercolor_palette());
  cv.film_relief = 0.f;
  WatercolorSim sim(cv);
  puddle(sim, 120, 120, 30, 0.10f, 2, 0.5f);
  sim.dry();
  // Pigment far outside the disc: none. Inside: present.
  CHECK(cv.pig(120, 120)[2] > 0.f);
  double outside = 0.0;
  for (int y = 0; y < cv.h; ++y)
    for (int x = 0; x < cv.w; ++x)
      if (sqr(static_cast<float>(x - 120)) + sqr(static_cast<float>(y - 120)) > sqr(48.f)) outside += cv.pig(x, y)[2];
  CHECK(outside == 0.0);
}

TEST(color_bleeds_into_wet_paper_but_not_dry) {
  auto spread = [](bool prewet) {
    Canvas cv(make_surface("hot_press", 240, 160, kMm, 3), watercolor_palette());
    cv.film_relief = 0.f;
    WatercolorSim sim(cv);
    if (prewet) {
      for (int y = 20; y < 140; ++y)
        for (int x = 20; x < 220; ++x) sim.water(x, y) = 0.10f;
      sim.touch({20, 20, 220, 140});
    }
    puddle(sim, 120, 80, 8, 0.10f, 5, 0.8f);
    sim.dry();
    int cells = 0;
    for (const PigVec& p : cv.pig.v) cells += p[5] > 0.004f;
    return cells;
  };
  int dry = spread(false), wet = spread(true);
  CHECK(dry > 100);
  CHECK(wet > 1.6 * dry);
}

TEST(a_wash_dries_with_a_darker_rim) {
  Canvas cv(make_surface("hot_press", 240, 240, kMm, 4), watercolor_palette());
  cv.film_relief = 0.f;
  WatercolorSim sim(cv);
  puddle(sim, 120, 120, 44, 0.12f, 3, 0.4f);
  sim.dry();
  double rim = 0.0, core = 0.0;
  int nr = 0, nc = 0;
  for (int y = 0; y < cv.h; ++y)
    for (int x = 0; x < cv.w; ++x) {
      float d = std::sqrt(sqr(static_cast<float>(x - 120)) + sqr(static_cast<float>(y - 120)));
      if (d < 20.f) { core += cv.pig(x, y)[3]; ++nc; }
      else if (d > 41.5f && d <= 44.f) { rim += cv.pig(x, y)[3]; ++nr; }
    }
  CHECK(rim / nr > 1.05 * core / nc);
}

TEST(granulating_pigment_settles_into_the_valleys) {
  // Ultramarine (granulating) against phthalo blue (not): how much more of
  // each lies in low cells than in high ones.
  auto valley_bias = [](int pigment) {
    Canvas cv(make_surface("rough", 200, 200, kMm, 5), watercolor_palette());
    cv.film_relief = 0.f;
    WatercolorSim sim(cv);
    puddle(sim, 100, 100, 60, 0.15f, pigment, 0.5f);
    sim.dry();
    double lo = 0.0, hi = 0.0;
    int nl = 0, nh = 0;
    for (int y = 60; y < 140; ++y)
      for (int x = 60; x < 140; ++x) {
        if (cv.tooth(x, y) < 0.f) { lo += cv.pig(x, y)[pigment]; ++nl; }
        else { hi += cv.pig(x, y)[pigment]; ++nh; }
      }
    return (lo / nl) / (hi / nh);
  };
  double ultramarine = valley_bias(4), phthalo = valley_bias(5);
  CHECK(ultramarine > 1.0);
  CHECK(ultramarine > phthalo);
}
