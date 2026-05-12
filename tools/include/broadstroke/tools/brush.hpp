#pragma once

#include <filesystem>
#include <string>

namespace broadstroke::tools {

// A wet-media brush. Unlike pencil, the brush is *separate* from the
// medium it carries — the same brush picks up burnt sienna, ultramarine,
// or whatever pigment the planner asks for. Pigment lives in PigmentPreset.
//
// Geometry: like the pencil tip, footprint is a soft Hermite-ramp disk
// (radius + softness). Bristle effects (splay, separation under load)
// are deferred to Stage 4+; for now a single round footprint is enough
// to demonstrate the wet/diffusion physics.
struct BrushPreset {
  std::string name;
  std::string kind;  // currently always "brush"

  // Footprint, identical semantics to PencilPreset's tip_radius_px /
  // tip_softness so the deposition kernel can share its falloff math.
  float tip_radius_px = 8.0f;
  float tip_softness  = 0.7f;

  // Per-unit-pressure water deposited into the wetness field. A unit of
  // wetness is "saturated puddle"; values < 1 are typical so the brush
  // wets the paper without flooding it.
  float water_load = 0.6f;

  // Per-unit-pressure pigment concentration deposited. Concentration is
  // a unitless scalar fed into the KM thickness factor; ~0.5 reads as
  // a moderately loaded wash, > 1 as opaque body color.
  float pigment_load = 0.8f;
};

BrushPreset loadBrush(const std::string& name);
BrushPreset loadBrushFromFile(const std::filesystem::path& path);

}  // namespace broadstroke::tools
