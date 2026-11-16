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
  float bring_out = 0.f;  // Settings::emphasis: how much the main subject (subject.focus) is brought out
  bool mute_rest = true;  // Settings::emphasis_mutes_rest
};

// What bringing out the main subject does to the artist's aim at a cell
// that belongs to it by `focus` (0..1), at `level` (Settings::emphasis):
// the factor on colorfulness, and how much more firmly local contrast is
// stated. With `mute_rest` the rest of the picture is aimed a little less
// colorful, the way a painter grays what is to stay back.
inline float focus_chroma(float level, float focus, bool mute_rest) {
  return std::max(0.f, 1.f + level * (0.18f * focus - (mute_rest ? 0.12f * (1.f - focus) : 0.f)));
}
inline float focus_contrast(float level, float focus) { return 0.3f * level * focus; }

// Returns the target picture (linear RGB) at canvas resolution.
Image make_target(const Subject& subject, const ToneIntent& intent, float mm_per_px);

// The subject as an artist who paints it as it is (oil) means to state it
// when the main subject is brought out: its colors, with those two changes.
Image bring_out(const Subject& subject, float level, bool mute_rest, float mm_per_px);

}  // namespace bs
