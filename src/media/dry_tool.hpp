// Dry drawing media: colored pencil and soft pastel, and the tools that move
// their marks around (stump, eraser).
//
// The tool is a solid with a real tip shape, stored as a height map: a
// sharpened cone held at an angle, or the flat side of a stick. Each step
// solves a contact problem between that tip and the paper's relief: the tool
// is pressed down until the contact pressure, summed over every cell where
// tip and paper interfere, balances the hand's force. A hard lead barely
// indents and touches only the crowns of the grain; soft wax or pastel
// conforms and reaches the valleys. Where there is pressure and sliding the
// tip abrades (Archard wear): material leaves the tip and stays on the paper
// in proportion to pressure times distance, less so once the surface is
// already coated and slick. The same volume is removed from the tip's height
// map, so a point wears to a facet and lines broaden until it is sharpened.
#pragma once

#include "canvas/canvas.hpp"
#include "motion/hand.hpp"

namespace bs {

struct DrySpec {
  enum class Tip { Cone, Bar };
  std::string name = "pencil";
  Tip tip = Tip::Cone;
  float core_mm = 2.f;        // Cone: diameter of the marking core. Bar: width
  float bar_len_mm = 20.f;    // Bar: length (along the lean direction)
  float cone_deg = 11.f;      // half-angle of the sharpened point
  float point_mm = 0.12f;     // radius of a fresh point, or of a bar's edges
  float tilt = 0.85f;         // how far the tool leans from vertical, radians
  Vec2 lean{0.7f, 0.7f};      // direction it leans toward (fixed by the grip)
  float stiffness = 400.f;    // contact stiffness, N/mm^3 (hard lead high, soft pastel low)
  float wear = 5e-4f;         // material transferred per unit pressure x sliding, mm^2/N
  float max_force = 3.f;      // normal force at full effort, N
  float fill_mm = 0.002f;     // coating thickness at which the surface stops taking more
  float adhesion_min = 0.08f; // residual transfer onto a fully coated surface
  float flow = 0.1f;          // share of each deposit pushed down into neighboring valleys
  float burnish = 0.f;        // how readily pressure works existing material down into the valleys
  float smear = 0.05f;        // how much existing material the tip drags along
  float lift = 0.f;           // how much existing material the tip removes (erasers)
  float pigment_load = 1.f;   // pigment share of the core; the rest is binder
  PigVec recipe{};            // pigment proportions (sum 1)
};

class DryTool {
 public:
  DryTool(const DrySpec& spec, float mm_per_px);

  // Restores the fresh tip shape.
  void sharpen();
  // Changes the grip (lean angle); rebuilds the tip.
  void set_tilt(float tilt);
  // Makes a stroke. When several tools work on separate parts of the canvas
  // at once, pass `changed`: the region marked is then added to it instead
  // of to the canvas's own record (see Canvas::mark_dirty_cells).
  void stroke(Canvas& canvas, const std::vector<Pose>& motion, Rect* changed = nullptr);

  // Hand settings appropriate to this tool's grip.
  HandParams hand() const;
  // How far from the tool's position, in cells, a stroke can change the canvas.
  float reach_px() const { return 0.5f * static_cast<float>(n_) + 2.f; }
  float travelled_mm() const { return travelled_mm_; }
  float deposited_mm3() const { return deposited_mm3_; }
  // Volume missing from the tip relative to a fresh one (equals what was laid down).
  float worn_mm3() const { return worn_mm3_; }

  DrySpec spec;

 private:
  void build_tip();
  void refresh_active();
  void step(Canvas& canvas, Vec2 pos, float force, float ds_mm, Rect& touched);

  float mm_per_px_;
  int n_ = 0;
  Plane<float> tip_;
  int ax0_ = 0, ay0_ = 0, ax1_ = 0, ay1_ = 0;  // box of tip cells that can touch
  float pen_ = 0.f;
  float carried_vol_ = 0.f;
  PigVec carried_pig_{};
  float travelled_mm_ = 0.f, deposited_mm3_ = 0.f, worn_mm3_ = 0.f;
  struct Cell { int x, y; float u, tx, ty; };
  std::vector<Cell> cells_;  // every canvas cell under the tip
  std::vector<Cell> near_;   // those close enough to the paper to matter, in the same order
  std::vector<float> near_u_;  // their heights, padded to a multiple of eight
  std::vector<float> near_d_;  // their contact depths at the solved tool height
};

// Hand settings appropriate to a tool of this kind.
HandParams dry_hand(const DrySpec& spec);

// Ready-made tools.
DrySpec colored_pencil_spec(const PigVec& recipe);
DrySpec pastel_spec(const PigVec& recipe, bool side);   // side of the stick or its corner
DrySpec pastel_pencil_spec(const PigVec& recipe);       // hard pastel in a wooden casing, for detail
DrySpec stump_spec(float width_mm);                     // paper stump / fingertip for blending
DrySpec eraser_spec(float width_mm);                    // kneaded eraser

}  // namespace bs
