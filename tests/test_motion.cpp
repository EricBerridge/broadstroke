// The hand: speed profile, corner behavior, loading and unloading, determinism.
#include "check.hpp"
#include "motion/hand.hpp"

using namespace bs;

namespace {
std::vector<Vec2> line(Vec2 a, Vec2 b, int n = 20) {
  std::vector<Vec2> p;
  for (int i = 0; i <= n; ++i) p.push_back(lerp(a, b, static_cast<float>(i) / n));
  return p;
}
float peak_speed(const std::vector<Pose>& m) {
  float v = 0.f;
  for (const Pose& p : m) v = std::max(v, p.speed);
  return v;
}
}  // namespace

TEST(stroke_starts_and_ends_where_intended) {
  const float mm = 0.25f;
  HandParams hand;
  Rng rng(1);
  Gesture g;
  g.path = line({100, 100}, {500, 140});
  auto m = perform(g, hand, mm, rng);
  CHECK(m.size() > 20);
  CHECK(length(m.front().pos - g.path.front()) * mm < 1.5f);
  CHECK(length(m.back().pos - g.path.back()) * mm < 6.f);
  // The tool is loaded gradually and unloaded at the end.
  CHECK(m.front().press < 0.2f);
  CHECK(m.back().press < 0.1f);
  float peak = 0.f;
  for (const Pose& p : m) {
    CHECK(p.press >= 0.f && p.press <= 1.f);
    peak = std::max(peak, p.press);
  }
  CHECK(peak > 0.4f);
}

TEST(short_strokes_are_slower_than_long_ones) {
  const float mm = 0.25f;
  HandParams hand;
  Rng rng(2);
  Gesture s, l;
  s.path = line({100, 100}, {124, 100});   // 6 mm
  l.path = line({100, 100}, {900, 100});   // 200 mm
  float vs = peak_speed(perform(s, hand, mm, rng)), vl = peak_speed(perform(l, hand, mm, rng));
  CHECK(vs < 0.75f * vl);
  CHECK(vl < 1.35f * hand.cruise_mm_s);
  CHECK(vl > 0.7f * hand.cruise_mm_s);
}

TEST(hand_slows_through_a_corner) {
  const float mm = 0.25f;
  HandParams hand;
  hand.tremor_mm = 0.f; hand.wander_mm = 0.f;
  Rng rng(3);
  Gesture g;
  g.path = line({100, 100}, {500, 100});
  auto second = line({500, 100}, {500, 500});
  g.path.insert(g.path.end(), second.begin() + 1, second.end());
  auto m = perform(g, hand, mm, rng);
  float near_corner = 1e9f, straight = 0.f;
  for (const Pose& p : m) {
    float d = length(p.pos - Vec2{500, 100}) * mm;
    if (d < 4.f) near_corner = std::min(near_corner, p.speed);
    if (d > 30.f) straight = std::max(straight, p.speed);
  }
  CHECK(near_corner < 0.6f * straight);
}

TEST(lean_follows_travel_for_brushes_and_stays_put_for_pencils) {
  const float mm = 0.25f;
  Rng rng(4);
  Gesture g;
  g.path = line({100, 400}, {100, 100});  // straight up
  HandParams brush;
  auto mb = perform(g, brush, mm, rng);
  CHECK(mb[mb.size() / 2].lean.y < -0.8f);
  HandParams pencil;
  pencil.lean_follows = false;
  pencil.fixed_lean = {0.7f, 0.7f};
  auto mp = perform(g, pencil, mm, rng);
  CHECK(mp[mp.size() / 2].lean.x > 0.6f && mp[mp.size() / 2].lean.y > 0.6f);
}

TEST(performance_is_deterministic) {
  const float mm = 0.25f;
  HandParams hand;
  Gesture g;
  g.path = line({50, 50}, {300, 220});
  Rng r1(77), r2(77), r3(78);
  auto a = perform(g, hand, mm, r1), b = perform(g, hand, mm, r2), c = perform(g, hand, mm, r3);
  CHECK(a.size() == b.size());
  bool same = a.size() == b.size(), differs = a.size() != c.size();
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) same = same && a[i].pos.x == b[i].pos.x && a[i].press == b[i].press;
  for (size_t i = 0; i < a.size() && i < c.size(); ++i) differs = differs || a[i].pos.x != c[i].pos.x;
  CHECK(same);
  CHECK(differs);
}
