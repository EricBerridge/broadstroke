#pragma once

#include "broadstroke/support/preset.hpp"
#include "broadstroke/support/support_data.hpp"

#include <cstdint>

namespace broadstroke::support {

// Procedurally populate every channel of `out` from `preset`. The seed
// is consumed by the noise hash so two runs with the same seed produce
// pixel-identical output — important for both reproducibility and the
// GPU-vs-CPU equivalence tests, which generate the same heightfield on
// both sides before validating the visualization.
//
// `out.width` and `out.height` must already be set; channel vectors
// are resized and overwritten.
void generate(SupportData& out, const Preset& preset, std::uint64_t seed);

}  // namespace broadstroke::support
