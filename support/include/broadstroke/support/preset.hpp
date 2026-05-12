#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace broadstroke::support {

// Knobs the procedural generators consume. Flat schema by design: one
// struct, three preset files that pick different values. New media that
// need fundamentally different parameters get their own struct later.
struct Preset {
  std::string name;

  // One of: "cold_press", "canvas", "smooth_bristol". Picks the
  // generator branch and is the only piece of behavior-affecting string.
  std::string kind;

  // Off-white base color of the dry substrate, linear RGB in [0, 1].
  std::array<float, 3> base_color = {0.96f, 0.94f, 0.90f};

  // Mean absorption coefficient in [0, 1]. Spatial variation is layered
  // on by the generator using the same noise field as the heightfield,
  // so dips and ridges share their wetness behavior.
  float absorption_base = 0.5f;

  // Heightfield is FBM noise in [-amplitude, +amplitude]. Amplitude is
  // unitless relative to the visualization's height_scale.
  float heightfield_amplitude = 0.5f;

  // FBM parameters. base_frequency is in cycles per pixel — 1/25
  // means one full noise cycle every 25 px at the coarsest octave.
  float noise_base_frequency = 0.04f;
  int   noise_octaves         = 5;
  float noise_persistence     = 0.5f;
  float noise_lacunarity      = 2.0f;

  // Weave (canvas-only when > 0). period_pixels is the thread spacing;
  // amplitude rides on top of the noise heightfield.
  float weave_period_pixels = 0.0f;
  float weave_amplitude     = 0.0f;

  // Grain anisotropy in [0, 1]. 0 = isotropic paper, 1 = fully aligned
  // (wood, woven canvas). direction is the principal axis in degrees,
  // measured counter-clockwise from +X.
  float grain_anisotropy        = 0.0f;
  float grain_direction_degrees = 0.0f;
};

// Resolve `name` to <kResourceDir>/supports/<name>.json and parse it.
// Throws std::runtime_error on missing file or schema violation.
Preset loadPreset(const std::string& name);

// Parse a preset from an explicit path. Useful for tests and tools.
Preset loadPresetFromFile(const std::filesystem::path& path);

}  // namespace broadstroke::support
