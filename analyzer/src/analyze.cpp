#include "broadstroke/analyzer/analyze.hpp"
#include "broadstroke/analyzer/luminance.hpp"

namespace broadstroke::analyzer {

Analysis analyze(const Image& img, const AnalyzeOptions& opt) {
  Analysis a;
  a.width  = img.width;
  a.height = img.height;
  a.num_bands = opt.num_bands;

  const std::size_t n = img.pixelCount();
  a.rgb.resize(n * 3);
  for (std::size_t i = 0; i < n; ++i) {
    a.rgb[i * 3 + 0] = img.rgba[i * 4 + 0];
    a.rgb[i * 3 + 1] = img.rgba[i * 4 + 1];
    a.rgb[i * 3 + 2] = img.rgba[i * 4 + 2];
  }

  a.luminance  = luminance(img);
  auto grad    = sobelGradient(a.luminance, img.width, img.height);
  a.edges      = sobelEdges(a.luminance, img.width, img.height);
  a.flow       = structureTensorFlow(grad, img.width, img.height,
                                     opt.flow_smooth_radius);
  a.value_band = valueBands(a.luminance, opt.num_bands);
  return a;
}

}  // namespace broadstroke::analyzer
