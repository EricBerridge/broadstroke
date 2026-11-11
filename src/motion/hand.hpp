// The artist's hand: turns an intended gesture into the motion a tool
// actually makes.
//
// A planned stroke is only a path and an effort. What reaches the paper
// depends on how a hand moves: it accelerates from rest and brakes into the
// end, slows through tight curvature (the two-thirds power law of human
// movement), tracks its aim point like a damped mass on a spring (so fast
// strokes cut corners and overshoot), trembles at 8-12 Hz, lands and lifts
// the tool over a finite time, and re-orients the tool's lean and twist with
// a lag. The tool simulations consume the resulting pose sequence.
#pragma once

#include "core/common.hpp"

namespace bs {

struct Gesture {
  std::vector<Vec2> path;       // intended path, canvas px
  std::vector<float> pressure;  // optional per-vertex effort 0..1
  float press = 0.6f;           // effort when `pressure` is empty
  float tempo = 1.f;            // speed multiplier (1 = the hand's natural pace)
  bool land = true;             // tool touches down at the start
  bool lift = true;             // tool lifts off at the end
};

struct HandParams {
  float cruise_mm_s = 170.f;   // speed on a long, straight run
  float accel_mm_s2 = 2600.f;  // how hard the arm accelerates and brakes
  float curve_radius_mm = 28.f; // radius above which curvature does not slow the hand
  float natural_hz = 6.f;      // tracking bandwidth of arm + tool
  float damping = 0.75f;       // damping ratio of that tracking
  float tremor_mm = 0.05f;     // physiological tremor amplitude
  float wander_mm = 0.35f;     // slow aiming error
  float press_noise = 0.07f;   // relative effort fluctuation
  float land_s = 0.06f;        // time to load the tool after touchdown
  float lift_mm = 6.f;         // travel over which the tool is unloaded at the end
  float tilt = 0.65f;          // lean of the handle from vertical, radians
  bool lean_follows = true;    // lean toward the direction of travel (brushes) or stay fixed (pencil grip)
  Vec2 fixed_lean{0.7f, 0.7f}; // lean direction when it does not follow
  float lean_hz = 2.5f;        // how quickly the lean re-orients
  float face_hz = 1.6f;        // how quickly a flat tool is rolled to face the travel
  float dt = 1.f / 400.f;      // sampling interval of the output, seconds
};

struct Pose {
  Vec2 pos;     // tool contact reference point, canvas px
  float press;  // applied effort 0..1
  float tilt;   // radians from vertical
  Vec2 lean;    // unit vector: horizontal direction the handle leans toward
  Vec2 face;    // unit vector: travel direction the tool's broad face is set for
  float speed;  // mm/s
};

std::vector<Pose> perform(const Gesture& g, const HandParams& hand, float mm_per_px, Rng& rng);

}  // namespace bs
