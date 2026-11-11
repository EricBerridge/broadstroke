// The artist's intent for a work on paper: the subject re-keyed into the
// range of tones the medium can actually produce on the chosen sheet.
#pragma once

#include "core/image.hpp"
#include "optics/spectrum.hpp"
#include "vision/analysis.hpp"

namespace bs {

struct ToneIntent {
  float l_dark = 0.2f;    // darkest lightness (Oklab L) the medium reaches on this paper
  float l_top = 0.95f;    // lightest: the bare paper, or an opaque light stick
  float chroma = 1.f;     // scaling of the subject's colorfulness
  float key = 1.f;        // > 1 opens the mid-tones toward the paper, keeping darks anchored
  float emphasis = 0.3f;  // how much local contrast is stated more firmly than in the photo
};

// Returns the target picture (linear RGB) at canvas resolution.
Image make_target(const Subject& subject, const ToneIntent& intent, float mm_per_px);

}  // namespace bs
