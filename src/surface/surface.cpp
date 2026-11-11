#include "surface/surface.hpp"

namespace bs {
namespace {

// Smooth value noise in [0, 1] on a unit lattice.
float value_noise(float x, float y, uint32_t seed) {
  int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
  float tx = x - xi, ty = y - yi;
  tx = tx * tx * tx * (tx * (tx * 6.f - 15.f) + 10.f);
  ty = ty * ty * ty * (ty * (ty * 6.f - 15.f) + 10.f);
  float a = hash01(xi, yi, seed), b = hash01(xi + 1, yi, seed);
  float c = hash01(xi, yi + 1, seed), d = hash01(xi + 1, yi + 1, seed);
  return lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
}

float fbm(float x, float y, int octaves, uint32_t seed) {
  float amp = 1.f, sum = 0.f, norm = 0.f;
  for (int o = 0; o < octaves; ++o) {
    sum += amp * value_noise(x, y, seed + 101u * o);
    norm += amp;
    amp *= 0.5f; x *= 2.03f; y *= 2.03f;
  }
  return sum / norm;
}

// Cellular bumps: 1 at a feature point falling to 0 at the cell border.
// Gives the pebbled surface that a papermaking felt presses into wet pulp.
float cellular(float x, float y, uint32_t seed) {
  int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
  float d1 = 9.f, d2 = 9.f;
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
      float px = xi + i + hash01(xi + i, yi + j, seed);
      float py = yi + j + hash01(xi + i, yi + j, seed + 7u);
      float d = sqr(px - x) + sqr(py - y);
      if (d < d1) { d2 = d1; d1 = d; }
      else if (d < d2) d2 = d;
    }
  d1 = std::sqrt(d1); d2 = std::sqrt(d2);
  return clamp01((d2 - d1) * 1.4f);  // ridge-free plateau around each point
}

// Adds paper fibers: short, slightly curved filaments lying in the sheet.
void add_fibers(Plane<float>& h, float mm_per_px, float per_mm2, float len_mm, float amp, Rng& rng) {
  float area = h.w * h.h * mm_per_px * mm_per_px;
  int count = static_cast<int>(area * per_mm2);
  for (int n = 0; n < count; ++n) {
    float x = rng.range(0.f, static_cast<float>(h.w)), y = rng.range(0.f, static_cast<float>(h.h));
    float ang = rng.range(0.f, 2.f * kPi);
    float curve = rng.normal() * 0.08f;
    float len = len_mm * rng.range(0.4f, 1.6f) / mm_per_px;
    float a = amp * rng.range(0.4f, 1.f) * (rng.uniform() < 0.5f ? -1.f : 1.f);
    int steps = std::max(2, static_cast<int>(len * 1.5f));
    float ds = len / steps;
    for (int s = 0; s < steps; ++s) {
      int ix = static_cast<int>(x), iy = static_cast<int>(y);
      if (h.inside(ix, iy)) h(ix, iy) += a * 0.67f;
      x += std::cos(ang) * ds; y += std::sin(ang) * ds;
      ang += curve * ds;
    }
  }
}

// Shifts to zero mean and scales so the 2nd..98th percentile spans +-relief.
void normalize_relief(Plane<float>& h, float relief) {
  double mean = 0.0;
  for (float v : h.v) mean += v;
  mean /= static_cast<double>(h.v.size());
  std::vector<float> sample;
  size_t stride = std::max<size_t>(1, h.v.size() / 20000);
  for (size_t i = 0; i < h.v.size(); i += stride) sample.push_back(h.v[i] - static_cast<float>(mean));
  std::sort(sample.begin(), sample.end());
  float lo = sample[sample.size() / 50], hi = sample[sample.size() - 1 - sample.size() / 50];
  float span = std::max(hi - lo, 1e-6f);
  float scale = 2.f * relief / span;
  for (float& v : h.v) v = (v - static_cast<float>(mean)) * scale;
}

struct PaperParams {
  float relief;      // mm
  float grain_mm;    // size of the felt/mould pattern
  float pebble;      // 0..1 share of cellular (pebbled) structure vs. cloudy
  float fibers;      // fibers per mm^2
  float fiber_amp;   // relative height of individual fibers
};

void make_paper(Surface& s, const PaperParams& p, uint32_t seed) {
  float inv = s.mm_per_px / p.grain_mm;
  parallel_for(0, s.h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < s.w; ++x) {
        float u = x * inv, v = y * inv;
        // Slight domain warp so the pattern never looks like a lattice.
        float wu = u + 0.35f * (value_noise(u * 0.7f, v * 0.7f, seed + 11u) - 0.5f);
        float wv = v + 0.35f * (value_noise(u * 0.7f, v * 0.7f, seed + 12u) - 0.5f);
        float cloud = fbm(wu * 1.3f, wv * 1.3f, 3, seed + 1u);
        float peb = cellular(wu, wv, seed + 2u);
        float fine = value_noise(u * 4.1f, v * 4.1f, seed + 3u);
        s.height(x, y) = lerp(cloud, peb, p.pebble) + 0.18f * fine;
      }
  }, 8);
  normalize_relief(s.height, 1.f);
  Rng rng(seed * 7919u + 13u);
  add_fibers(s.height, s.mm_per_px, p.fibers, 2.2f, p.fiber_amp, rng);
  gaussian_blur(s.height, 0.45f);
  normalize_relief(s.height, p.relief);
  s.relief = p.relief;
}

// Plain weave: warp threads run vertically, weft horizontally, each passing
// alternately over and under the other. The surface is the upper envelope of
// the two thread families, then partly filled by the priming coat.
void make_canvas(Surface& s, float thread_mm, float relief, float priming, uint32_t seed) {
  float p = thread_mm / s.mm_per_px;  // thread pitch in px
  float r = 0.5f;                     // thread radius in pitch units
  parallel_for(0, s.h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < s.w; ++x) {
        float u = x / p, v = y / p;
        // Threads wander and vary in thickness along their length (slubs).
        float uw = u + 0.16f * (value_noise(u * 0.9f, v * 0.11f, seed + 21u) - 0.5f);
        float vw = v + 0.16f * (value_noise(u * 0.11f, v * 0.9f, seed + 22u) - 0.5f);
        int i = static_cast<int>(std::floor(uw)), j = static_cast<int>(std::floor(vw));
        float fu = uw - i - 0.5f, fv = vw - j - 0.5f;  // offset from thread axes
        float tw = 0.75f + 0.5f * value_noise(i * 3.3f + 0.5f, v * 0.23f, seed + 23u);
        float tf = 0.75f + 0.5f * value_noise(u * 0.23f, j * 3.3f + 0.5f, seed + 24u);
        // Warp thread i: its axis rises over weft j when (i + j) is even.
        float warp_axis = 0.5f * std::cos(kPi * (vw - 0.5f) + kPi * static_cast<float>(i));
        float weft_axis = -0.5f * std::cos(kPi * (uw - 0.5f) + kPi * static_cast<float>(j));
        float rw = r * tw, rf = r * tf;
        float warp = warp_axis + std::sqrt(std::max(rw * rw - fu * fu, 0.f)) - (std::abs(fu) > rw ? 2.f : 0.f);
        float weft = weft_axis + std::sqrt(std::max(rf * rf - fv * fv, 0.f)) - (std::abs(fv) > rf ? 2.f : 0.f);
        float hgt = std::max(std::max(warp, weft), -0.6f);
        // Yarn fuzz.
        hgt += 0.10f * (value_noise(x * 0.9f, y * 0.9f, seed + 25u) - 0.5f);
        s.height(x, y) = hgt;
      }
  }, 8);
  // Priming (gesso) settles into the interstices: clamp the valleys.
  normalize_relief(s.height, 1.f);
  float floor_level = -1.f + 1.4f * priming;
  for (float& hv : s.height.v) {
    if (hv < floor_level) hv = floor_level + 0.25f * (hv - floor_level);
  }
  gaussian_blur(s.height, 0.5f + 0.4f * priming);
  normalize_relief(s.height, relief);
  s.relief = relief;
}

// Gessoed panel: nearly flat, with the long shallow ridges a priming brush leaves.
void make_panel(Surface& s, float relief, uint32_t seed) {
  float inv = s.mm_per_px;
  parallel_for(0, s.h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < s.w; ++x) {
        float u = x * inv, v = y * inv;
        float streak = fbm(u * 0.05f, v * 2.2f, 3, seed + 31u);
        float tooth = value_noise(u * 6.f, v * 6.f, seed + 32u);
        s.height(x, y) = streak + 0.35f * tooth;
      }
  }, 8);
  normalize_relief(s.height, relief);
  s.relief = relief;
}

}  // namespace

std::vector<std::string> surface_names() {
  return {"cold_press", "hot_press", "rough", "drawing", "bristol",
          "pastel_paper", "canvas", "canvas_fine", "panel"};
}

bool is_surface_name(const std::string& name) {
  for (const auto& n : surface_names())
    if (n == name) return true;
  return false;
}

Surface make_surface(const std::string& name_in, int w, int h, float mm_per_px, uint32_t seed) {
  std::string name = is_surface_name(name_in) ? name_in : "drawing";
  Surface s;
  s.name = name;
  s.w = w; s.h = h; s.mm_per_px = mm_per_px;
  s.height = Plane<float>(w, h, 0.f);
  Rgb color{0.86f, 0.85f, 0.80f};
  if (name == "cold_press") {
    make_paper(s, {0.060f, 0.85f, 0.75f, 2.5f, 0.10f}, seed);
    s.absorbency = 0.55f; s.grip = 0.75f; color = {0.90f, 0.885f, 0.83f};
  } else if (name == "rough") {
    make_paper(s, {0.110f, 1.35f, 0.85f, 2.5f, 0.08f}, seed);
    s.absorbency = 0.6f; s.grip = 0.9f; color = {0.90f, 0.88f, 0.82f};
  } else if (name == "hot_press") {
    make_paper(s, {0.010f, 0.6f, 0.3f, 4.f, 0.35f}, seed);
    s.absorbency = 0.4f; s.grip = 0.35f; color = {0.91f, 0.90f, 0.86f};
  } else if (name == "bristol") {
    make_paper(s, {0.005f, 0.5f, 0.2f, 4.f, 0.4f}, seed);
    s.absorbency = 0.25f; s.grip = 0.25f; color = {0.93f, 0.93f, 0.91f};
  } else if (name == "drawing") {
    make_paper(s, {0.022f, 0.42f, 0.6f, 4.f, 0.25f}, seed);
    s.absorbency = 0.5f; s.grip = 0.6f; color = {0.90f, 0.89f, 0.85f};
  } else if (name == "pastel_paper") {
    make_paper(s, {0.045f, 0.55f, 0.9f, 3.f, 0.15f}, seed);
    s.absorbency = 0.5f; s.grip = 0.95f; color = {0.62f, 0.58f, 0.52f};
  } else if (name == "canvas") {
    make_canvas(s, 0.95f, 0.11f, 0.45f, seed);
    s.absorbency = 0.3f; s.grip = 0.8f; color = {0.90f, 0.89f, 0.85f};
  } else if (name == "canvas_fine") {
    make_canvas(s, 0.6f, 0.055f, 0.6f, seed);
    s.absorbency = 0.25f; s.grip = 0.65f; color = {0.91f, 0.90f, 0.86f};
  } else {  // panel
    make_panel(s, 0.02f, seed);
    s.absorbency = 0.2f; s.grip = 0.5f; color = {0.92f, 0.915f, 0.89f};
  }
  s.reflectance = rgb_to_spectrum(color);
  return s;
}

}  // namespace bs
