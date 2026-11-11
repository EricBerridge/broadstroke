#include "artist/target.hpp"

namespace bs {

Image make_target(const Subject& subject, const ToneIntent& in, float mm_per_px) {
  const int w = subject.color.w, h = subject.color.h;
  // Percentile range of the subject's lightness.
  std::vector<float> ls;
  size_t stride = std::max<size_t>(1, subject.light.v.size() / 50000);
  for (size_t i = 0; i < subject.light.v.size(); i += stride) ls.push_back(subject.light.v[i]);
  std::sort(ls.begin(), ls.end());
  float lo = ls[ls.size() * 2 / 100], hi = ls[ls.size() * 98 / 100];
  float span = std::max(hi - lo, 0.05f);
  // A drawing states local contrast more firmly than a photograph does:
  // each point is judged against its surroundings (a few mm), which is what
  // makes features read once the overall key has been lightened.
  Plane<float> surround = subject.light;
  gaussian_blur(surround, std::max(2.f, 2.2f / mm_per_px));
  Image target(w, h);
  parallel_for(0, h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        Lab s = rgb_to_oklab(subject.color(x, y));
        float u = clamp01((s.L - lo) / span);
        u = 1.f - std::pow(1.f - u, in.key);
        u = clamp01(u + in.emphasis * (s.L - surround(x, y)) / span);
        Lab t;
        t.L = in.l_dark + (in.l_top - in.l_dark) * u;
        if (in.mono) { t.a = in.paper.a; t.b = in.paper.b; }
        else { t.a = s.a * in.chroma; t.b = s.b * in.chroma; }
        target(x, y) = oklab_to_rgb(t);
      }
  }, 8);
  gaussian_blur(target, std::max(0.7f, 0.25f / mm_per_px));
  return target;
}

}  // namespace bs
