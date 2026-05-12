#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace broadstroke::tools {

// Combined tool + medium description for a pencil. Graphite is fused to
// the tip of a real pencil — you don't dip-and-swap — so for Stage 2 we
// model the pair as a single preset. Watercolor in Stage 3 will properly
// split the brush (tool) from the pigment (medium).
struct PencilPreset {
  std::string name;
  std::string kind;  // currently always "pencil"

  // Stamp footprint. tip_radius is in canvas pixels at 1024x1024; softness
  // in [0, 1] controls how quickly the stamp fades to zero. softness=0
  // gives a hard disk, softness=1 a fully smooth Hermite ramp from
  // center to edge.
  float tip_radius_px = 1.5f;
  float tip_softness  = 0.5f;

  // How much graphite the tip sheds per unit (pressure * tooth * arc).
  // Softer leads (2B, 4B) crumble faster and have higher values; harder
  // leads (2H, 4H) lower. Scale is intentionally unitless — calibrate by
  // eye against a stroke at pressure=1 on cold-press paper.
  float deposit_strength = 1.0f;

  // Graphite tone in linear RGB [0, 1]. Most real graphite reads almost
  // neutral with a faint warm cast on softer leads.
  std::array<float, 3> color = {0.10f, 0.09f, 0.09f};
};

// Resolve `name` to <kResourceDir>/tools/<name>.json and parse it.
PencilPreset loadPencil(const std::string& name);

// Parse a pencil preset from an explicit path. Useful for tests.
PencilPreset loadPencilFromFile(const std::filesystem::path& path);

}  // namespace broadstroke::tools
