#pragma once

#include "broadstroke/analyzer/edges.hpp"
#include "broadstroke/analyzer/flow.hpp"
#include "broadstroke/analyzer/image.hpp"

#include <cstdint>
#include <vector>

namespace broadstroke::analyzer {

// Bundle of every per-pixel field the planner consumes. Produced once
// per source photo by analyze(); cheap to keep around since it's just
// six W*H float buffers + one byte-per-pixel band map.
struct Analysis {
  std::uint32_t width  = 0;
  std::uint32_t height = 0;

  // Mean linear-RGB color per pixel — the source image's color, kept on
  // the analysis so the planner can sample it without retaining the
  // original Image. Same layout as Image::rgba but RGB only (3*W*H).
  std::vector<float> rgb;

  std::vector<float>        luminance;   // Rec. 709 Y, [0, 1]
  std::vector<float>        edges;       // Sobel magnitude
  FlowField                 flow;        // unit vectors along texture
  std::vector<std::uint8_t> value_band;  // [0, num_bands - 1]
  std::uint8_t              num_bands = 0;
};

struct AnalyzeOptions {
  std::uint8_t num_bands = 4;            // shadows / mid-low / mid-high / lights
  int          flow_smooth_radius = 3;   // structure-tensor pre-blur radius
};

Analysis analyze(const Image& img, const AnalyzeOptions& opt = {});

}  // namespace broadstroke::analyzer
