// The support the artist works on: paper, canvas or panel.
//
// A surface is a height field in millimeters at the simulation's cell size.
// Nothing here is a decorative texture: tools ride on this relief, deposit on
// its peaks first, fill its valleys under pressure, and pool water in it.
#pragma once

#include <string>
#include <vector>

#include "core/image.hpp"
#include "optics/spectrum.hpp"

namespace bs {

struct Surface {
  std::string name;
  int w = 0, h = 0;
  float mm_per_px = 0.25f;
  Plane<float> height;    // mm, zero mean
  float relief = 0.05f;   // nominal peak amplitude of the texture, mm
  Spectrum reflectance{}; // color of the bare surface
  float absorbency = 0.5f; // 0 = hard sized / primed, 1 = blotter
  float grip = 0.5f;       // how strongly the surface abrades dry media (0..1)
};

// Known names: cold_press, hot_press, rough, drawing, bristol, pastel_paper,
// canvas, canvas_fine, panel.
std::vector<std::string> surface_names();
bool is_surface_name(const std::string& name);

// Generates the named surface procedurally. Deterministic in (name, size,
// mm_per_px, seed). Unknown names fall back to "drawing".
Surface make_surface(const std::string& name, int w, int h, float mm_per_px, uint32_t seed = 1);

}  // namespace bs
