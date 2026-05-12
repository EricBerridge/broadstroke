#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace broadstroke::tools {

// Pastel stick. Like pencil, the pigment is bound to the tool — you don't
// load a separate medium into a pastel. Uses the same KM-pigment compose
// path as watercolor (deposits into a pigment field), with KM K/S
// coefficients carried directly on the preset.
struct PastelPreset {
  std::string name;
  std::string kind;  // "pastel"

  float tip_radius_px    = 6.0f;
  float tip_softness     = 0.65f;
  float deposit_strength = 1.4f;

  // KM coefficients (per RGB channel) for this stick's pigment binder.
  std::array<float, 3> K = {0.5f, 0.5f, 0.5f};
  std::array<float, 3> S = {0.6f, 0.6f, 0.6f};

  // Optional smear used by the demo when this stick is dragged over
  // existing pigment. Smear strength 0 disables.
  float smear_radius_px = 8.0f;
  float smear_softness  = 0.7f;
  std::uint32_t smear_taps = 6;
  float smear_strength  = 0.6f;
};

PastelPreset loadPastel(const std::string& name);
PastelPreset loadPastelFromFile(const std::filesystem::path& path);

}  // namespace broadstroke::tools
