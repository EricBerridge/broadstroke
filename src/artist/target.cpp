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
  const bool focused = in.bring_out > 0.f && !subject.focus.empty();
  Image target(w, h);
  parallel_for(0, h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        Lab s = rgb_to_oklab(subject.color(x, y));
        float u = clamp01((s.L - lo) / span);
        u = 1.f - std::pow(1.f - u, in.key);
        float emphasis = in.emphasis, chroma = in.chroma;
        if (focused) {
          const float f = subject.focus(x, y);
          emphasis += focus_contrast(in.bring_out, f);
          chroma *= focus_chroma(in.bring_out, f, in.mute_rest);
        }
        u = clamp01(u + emphasis * (s.L - surround(x, y)) / span);
        Lab t;
        t.L = in.l_dark + (in.l_top - in.l_dark) * u;
        t.a = s.a * chroma;
        t.b = s.b * chroma;
        target(x, y) = oklab_to_rgb(t);
      }
  }, 8);
  gaussian_blur(target, std::max(0.7f, 0.25f / mm_per_px));
  return target;
}

Image bring_out(const Subject& subject, float level, bool mute_rest, float mm_per_px) {
  const int w = subject.color.w, h = subject.color.h;
  if (level <= 0.f || subject.focus.empty()) return subject.color;
  Plane<float> surround = subject.light;
  gaussian_blur(surround, std::max(2.f, 2.2f / mm_per_px));
  Image aim(w, h);
  parallel_for(0, h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        Lab s = rgb_to_oklab(subject.color(x, y));
        const float f = subject.focus(x, y), chroma = focus_chroma(level, f, mute_rest);
        s.L = clamp01(s.L + focus_contrast(level, f) * (s.L - surround(x, y)));
        s.a *= chroma;
        s.b *= chroma;
        aim(x, y) = oklab_to_rgb(s);
      }
  }, 8);
  return aim;
}

}  // namespace bs
