#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace broadstroke::tools {

// A pigment described by its Kubelka-Munk coefficients sampled at three
// wavelengths (R, G, B). Real pigments need many more samples for accurate
// color reproduction — three is the minimum that lets pigments mix
// subtractively with substantially correct hue shifts (mixing ultramarine
// and cadmium yellow gives green, not grey, because their K and S have
// different per-channel ratios). A spectral upgrade (8-16 samples) is a
// Stage 7+ concern.
//
//   K[c] = absorption  coefficient at channel c (higher = darker)
//   S[c] = scattering  coefficient at channel c (higher = more opaque)
//
// Both are unitless. The KM apply kernel scales them by the per-pixel
// concentration field, then derives reflectance.
struct PigmentPreset {
  std::string name;
  std::string kind;  // currently always "pigment"

  std::array<float, 3> K = {0.5f, 0.5f, 0.5f};
  std::array<float, 3> S = {0.5f, 0.5f, 0.5f};

  // Display swatch color (linear RGB). Used by tooling that wants to
  // render a label or thumbnail without running the KM compose; not
  // consumed by the simulation kernels.
  std::array<float, 3> swatch = {0.5f, 0.5f, 0.5f};
};

PigmentPreset loadPigment(const std::string& name);
PigmentPreset loadPigmentFromFile(const std::filesystem::path& path);

}  // namespace broadstroke::tools
