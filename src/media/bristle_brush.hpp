// Bristle brush and viscous paint (oil, acrylic, gouache).
//
// The brush is a bundle of individually simulated bristles. Each bristle is
// an elastic filament anchored in the ferrule: pressing the brush down bends
// it so its end lies on the canvas and trails behind the anchor like a towed
// line, springing back toward the handle according to its stiffness. Each
// bristle carries its own paint (volume and pigment composition), fed from
// the brush's belly, and exchanges paint with every canvas cell it slides
// over: it leaves a lubrication film whose thickness falls as the bristle
// runs dry, bridges into the valleys of the weave only while it has enough
// paint to do so, picks up and re-deposits wet paint already on the canvas,
// and ploughs thick paint aside into ridges. Stroke shape, streaks, dry-brush
// breakup, color pickup and impasto all come out of that exchange; none of
// them is drawn.
#pragma once

#include "canvas/canvas.hpp"
#include "motion/hand.hpp"

namespace bs {

struct BrushSpec {
  enum class Shape { Flat, Filbert, Round };
  std::string name = "flat";
  Shape shape = Shape::Flat;
  float width_mm = 12.f;      // across the ferrule (diameter for a round)
  float thick_mm = 3.f;       // ferrule thickness (ignored for a round)
  float length_mm = 16.f;     // free bristle length
  float stiffness = 1.f;      // 1 = hog bristle, ~0.3 = soft sable
  float spring = 0.5f;        // how firmly bent bristles re-align behind the handle
  float splay = 0.5f;         // spreading of the tips under compression
  float clump = 0.5f;         // tendency to gather into tufts as the load runs low
  float max_depth_mm = 4.f;   // compression of the bristles at full effort
  float taper = 0.3f;         // round brushes: how much shorter the outer hairs are (the point)
};

// Rheology of the paint as it comes off the brush.
struct PaintBody {
  float coat_mm = 0.065f;     // film a fully charged bristle leaves behind
  float rate = 0.8f;          // exchange per px of sliding at unit pressure
  float pickup = 0.45f;       // share of scraped-off excess the bristle takes up
  float plough = 0.4f;        // share of scraped-off excess pushed aside as ridges
  float mix = 0.6f;           // blending between bristle load and wet film on contact
  float tip_hold_mm = 0.2f;   // film thickness a bristle tip can carry
  float feed = 0.18f;         // refill of the tip from the belly, per px of travel
  float bridge = 0.9f;        // how far a carried film reaches into surface valleys
  float level = 0.85f;        // how much the film fills valleys rather than following the relief
};

// Where a brush working in water puts its load: the free water and suspended
// pigment fields of a watercolor simulation, instead of the canvas film.
struct WetSink {
  Plane<float>* water = nullptr;
  Plane<PigVec>* susp = nullptr;
};

class BristleBrush {
 public:
  BristleBrush(const BrushSpec& spec, float mm_per_px, uint32_t seed);

  // Charges the brush. `frac` are pigment proportions of the mix (sum 1),
  // `volume_mm3` the amount of paint taken up, `medium` the share of that
  // volume that is clear vehicle (thinner or oil) rather than pigmented paste.
  // Paint already in the brush stays and mixes with the new load.
  void load(const PigVec& frac, float volume_mm3, float medium = 0.f);
  // Charges the brush with watercolor: `volume_mm3` of water carrying the
  // given pigment amounts per mm of water depth.
  void load_wet(const PigVec& pigment_per_mm, float volume_mm3);
  // Makes the brush deposit into a watercolor simulation (pass {} to go back to paint).
  void set_wet(const WetSink& sink) { wet_ = sink; }
  // Cells touched by the last stroke.
  Rect last_touched() const { return last_touched_; }
  // Wipes the brush on a rag, keeping the given share of what it carried.
  void wipe(float keep = 0.1f);
  float carried_mm3() const;
  // Paint needed to wet all the bristle tips.
  float tip_capacity_mm3() const;

  // Executes one stroke along the given hand motion.
  void stroke(Canvas& canvas, const std::vector<Pose>& motion);

  const BrushSpec& spec() const { return spec_; }
  PaintBody body;
  int bristle_count() const { return static_cast<int>(bristles_.size()); }

 private:
  struct Bristle {
    Vec2 root;        // offset in the ferrule frame, px (x across, y through the thickness)
    float len;        // mm
    float stiff;      // relative stiffness
    float tuft_x;     // x of the tuft this bristle gathers into, px
    Vec2 tip;         // current tip position, canvas px
    Vec2 dir;         // unit direction from anchor to tip
    float prev_delta; // compression at the previous step, mm
    bool touching;
    float vol;        // carried paint, mm of film over one cell
    PigVec pig;       // carried pigment
  };
  void step(Canvas& canvas, const Pose& pose, float move_px, Rect& touched);

  BrushSpec spec_;
  float mm_per_px_;
  float max_len_ = 0.f;
  float tip_spread_x_ = 1.f, tip_spread_y_ = 0.4f;
  std::vector<Bristle> bristles_;
  float belly_vol_ = 0.f;
  PigVec belly_pig_{};
  float cap_ = 0.f;  // per-bristle carrying capacity, cell-mm
  uint32_t tick_ = 0;
  WetSink wet_;
  Rect last_touched_;
  void wet_tips();
};

}  // namespace bs
