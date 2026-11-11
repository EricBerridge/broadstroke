// Watercolor: water and pigment on paper.
//
// A wash is a shallow film of water lying on, and soaking into, the sheet.
// The simulation tracks, per cell: free water on the surface, the dampness
// of the paper beneath it, pigment still suspended in the water, and pigment
// already settled on the fibers (the canvas film). Each time step:
//   * water runs downhill on its own free surface (paper relief + depth), so
//     it pools in the valleys of the grain; it cannot cross onto dry paper
//     until the head overcomes the contact line's pinning, which is what
//     gives wet-on-dry strokes their hard edges;
//   * suspended pigment is carried with the water and diffuses through it,
//     which is what makes wet-in-wet color bleed and feather;
//   * the paper soaks up water and dampness creeps outward through it;
//   * water evaporates, fastest at the edge of a wet area, so water (and the
//     pigment in it) flows outward to replace it and dries as a dark rim;
//   * pigment settles onto the paper, heavier and more granulating pigments
//     faster and preferentially into the valleys; non-staining pigments are
//     partly lifted again when water returns (the cause of backruns).
// None of these effects is drawn; they emerge from the transport.
#pragma once

#include "canvas/canvas.hpp"

namespace bs {

struct WaterParams {
  float dt = 0.3f;            // s
  float flow = 2.2f;          // free-surface leveling rate, 1/s
  float pin_mm = 0.18f;       // head needed to advance onto dry paper (a bead this deep is held by surface tension)
  float relief_share = 0.35f; // how much of the grain the free surface follows (surface tension spans the rest)
  float damp = 0.45f;         // paper saturation above which water spreads freely
  float capacity_mm = 0.07f;  // water the paper holds at full saturation
  float absorb = 0.20f;       // uptake rate into the paper, 1/s (scaled by absorbency)
  float wick = 0.06f;         // spreading of dampness through the paper, 1/s
  float evaporate = 0.0011f;  // mm/s from an open film
  float edge_boost = 6.0f;    // extra evaporation at the rim of a wet area
  float diffuse = 0.5f;       // pigment diffusion in the water, 1/s
  float settle = 0.009f;      // pigment settling rate, 1/s at the reference depth
  float stain = 0.035f;       // binding of staining pigment to the fibers, 1/s (any depth, crown or valley)
  float lift = 0.004f;        // re-suspension of settled, non-staining pigment, 1/s
  float depth_ref_mm = 0.1f;
};

class WatercolorSim {
 public:
  explicit WatercolorSim(Canvas& canvas);

  Plane<float> water;    // free surface water, mm
  Plane<float> sat;      // paper saturation, 0..1
  Plane<PigVec> susp;    // pigment suspended in the surface water
  WaterParams params;

  // Tells the simulation that cells in this region were wetted. It must be
  // called after writing to `water` or `susp`: the simulation relies on water
  // and suspended pigment existing only in regions it has been told about.
  void touch(const Rect& r);
  // Advances the simulation by the given time.
  void advance(float seconds);
  // Lets everything dry: simulates until no free water remains (bounded by
  // max_seconds, after which the remainder is evaporated in place).
  void dry(float max_seconds = 240.f);
  bool any_wet() const;

  // Totals, for conservation checks.
  double total_water() const;       // surface water + water held in the paper, mm summed over cells
  PigVec total_pigment() const;     // suspended + settled

 private:
  void step();
  static constexpr int kTile = 32;
  Canvas& cv_;
  int tw_ = 0, th_ = 0;
  std::vector<uint8_t> active_;  // per tile: 0 idle, 1 damp, 2 touched or holds free water
  Plane<std::array<float, 4>> out_;  // share of a cell's water leaving toward +x, -x, +y, -y
  Plane<float> water_n_;
  Plane<PigVec> susp_n_;
  Plane<float> sat_n_;
};

}  // namespace bs
