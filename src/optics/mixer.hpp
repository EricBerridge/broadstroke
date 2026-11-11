// Color matching: how the artist decides what to put on the brush.
//
// A painter cannot ask for an RGB value. They mix tube colors until the mix
// looks right. The Mixer does the same: it searches pigment proportions whose
// Kubelka-Munk result is perceptually closest to a wanted color, either as an
// opaque body color (oil) or as a transparent film over what is already on
// the paper (watercolor glaze). Colors outside the palette's gamut get the
// nearest achievable mix, exactly as they would on a real palette.
#pragma once

#include <unordered_map>

#include "optics/pigment.hpp"

namespace bs {

class Mixer {
 public:
  explicit Mixer(const Palette& palette);

  // Proportions (summing to 1) of an opaque mix matching the target color.
  // Results are cached; not thread-safe.
  PigVec match_opaque(Rgb target) const;

  // Pigment amounts for a transparent film that, laid over a surface with
  // reflectance `base`, comes as close as possible to the target color.
  // A film can only darken, so targets lighter than the base return zero.
  PigVec match_glaze(const Spectrum& base, Rgb target, float max_total) const;

  Rgb masstone_rgb(const PigVec& frac) const { return spectrum_to_rgb(pal_.masstone(frac)); }
  const Palette& palette() const { return pal_; }

 private:
  struct Candidate {
    PigVec frac;
    Lab lab;
  };
  struct Chroma {
    PigVec frac;  // normalized
    Spectrum k, s;
  };
  Lab opaque_lab(const PigVec& frac) const;
  Lab glaze_lab(const Spectrum& base, const PigVec& amount) const;

  Palette pal_;
  std::vector<Candidate> opaque_;
  std::vector<Chroma> chroma_;
  mutable std::unordered_map<uint32_t, PigVec> cache_;
};

}  // namespace bs
