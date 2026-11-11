// Pigments and palettes.
#pragma once

#include <string>

#include "optics/spectrum.hpp"

namespace bs {

// One pigment: optical coefficients per unit film thickness (1/mm for paint
// and dry media; per unit surface load for watercolor) plus the physical
// traits the watercolor simulation needs.
struct Pigment {
  std::string name;
  Spectrum K{};             // absorption
  Spectrum S{};             // scattering
  float granulation = 0.f;  // 0..1: how readily particles settle into paper valleys
  float staining = 0.f;     // 0..1: how strongly settled particles resist re-wetting
  float density = 1.f;      // relative particle weight (settling rate)
};

enum class PigmentId {
  TitaniumWhite,
  CadmiumYellow,
  YellowOchre,
  CadmiumRed,
  AlizarinCrimson,
  UltramarineBlue,
  PhthaloBlue,
  PhthaloGreen,
  BurntSienna,
  BurntUmber,
  IvoryBlack,
};

// strength scales K and S together (pigment concentration in the vehicle);
// scatter scales S alone (lower for transparent vehicles such as gum arabic).
Pigment make_pigment(PigmentId id, float strength = 1.f, float scatter = 1.f);

struct Palette {
  std::string name;
  int n = 0;
  std::array<Pigment, kMaxPig> pig;
  int white = -1;  // index of an opaque white, or -1 (watercolor uses the paper)

  void add(const Pigment& p, bool is_white = false) {
    if (n >= kMaxPig) return;
    if (is_white) white = n;
    pig[n++] = p;
  }
  // Optical thickness (K d, S d) of a film holding the given pigment amounts.
  void film(const PigVec& amount, Spectrum& kd, Spectrum& sd) const {
    Vec8 k = _mm256_setzero_ps(), s = _mm256_setzero_ps();
    for (int i = 0; i < n; ++i) {
      float a = amount[i];
      if (a <= 0.f) continue;
      Vec8 av = splat8(a);
      k = _mm256_add_ps(k, _mm256_mul_ps(av, load8(pig[i].K)));
      s = _mm256_add_ps(s, _mm256_mul_ps(av, load8(pig[i].S)));
    }
    store8(kd, k);
    store8(sd, s);
  }
  // Body color of an opaque mix with the given proportions.
  Spectrum masstone(const PigVec& frac) const {
    Spectrum kd, sd, r;
    film(frac, kd, sd);
    for (int b = 0; b < kBands; ++b) r[b] = km_masstone_band(kd[b], sd[b]);
    return r;
  }
};

// Artist's oil colors: a classic eight-tube palette. K, S per mm of paint.
Palette oil_palette();
// Transparent watercolors; amounts are surface loads where 1.0 is a strong wash.
Palette watercolor_palette();
// Base pigments from which colored sticks (colored pencil, pastel)
// are compounded. K, S per mm of pure pigment.
Palette stick_palette();

}  // namespace bs
