#include "canvas/canvas.hpp"

#include <cstring>

namespace bs {

Canvas::Canvas(Surface surface_in, Palette palette_in)
    : w(surface_in.w), h(surface_in.h), mm_per_px(surface_in.mm_per_px),
      surface(std::move(surface_in)), palette(std::move(palette_in)),
      base(w, h, surface.reflectance), dry_h(w, h, 0.f), pig(w, h, PigVec{}), vol(w, h, 0.f),
      tooth(w, h, 0.f) {
  dirty_ = Plane<uint8_t>(w, h, 0);
  refresh_tooth();
}

void Canvas::mark_dirty(const Rect& r_in) {
  Rect r = r_in.clipped(w, h);
  if (r.empty()) return;
  for (int y = r.y0; y < r.y1; ++y) std::memset(&dirty_(r.x0, y), 1, static_cast<size_t>(r.width()));
  dirty_bounds_.include(r);
}

void Canvas::clear_dirty() {
  std::fill(dirty_.v.begin(), dirty_.v.end(), static_cast<uint8_t>(0));
  dirty_bounds_ = Rect{};
  dirty_box_ = Rect{};
}

void Canvas::refresh_albedo(Image& out) {
  if (!dirty_box_.empty()) {
    render_albedo(dirty_box_, out);
    dirty_box_ = Rect{};
  }
  if (dirty_bounds_.empty()) return;
  // Runs of marked cells along each row.
  struct Run { int y, x0, x1; };
  std::vector<Run> runs;
  int cells = 0;
  const Rect b = dirty_bounds_;
  for (int y = b.y0; y < b.y1; ++y) {
    uint8_t* row = &dirty_(0, y);
    int x = b.x0;
    while (x < b.x1) {
      const void* hit = std::memchr(row + x, 1, static_cast<size_t>(b.x1 - x));
      if (!hit) break;
      int start = static_cast<int>(static_cast<const uint8_t*>(hit) - row);
      x = start;
      while (x < b.x1 && row[x]) row[x++] = 0;
      runs.push_back({y, start, x});
      cells += x - start;
    }
  }
  dirty_bounds_ = Rect{};
  auto body = [&](int lo, int hi) {
    for (int i = lo; i < hi; ++i) {
      const Run& r = runs[i];
      for (int x = r.x0; x < r.x1; ++x) out(x, r.y) = albedo(x, r.y);
    }
  };
  const int n = static_cast<int>(runs.size());
  if (cells > 20000) parallel_for(0, n, body, 16);
  else body(0, n);
}

void Canvas::refresh_tooth() {
  // Local mean over ~2.5 mm: the scale a brush tip or pastel face averages.
  Plane<float> mean(w, h);
  for (size_t i = 0; i < mean.v.size(); ++i) mean.v[i] = surface.height.v[i] + dry_h.v[i];
  tooth = mean;
  gaussian_blur(mean, std::max(1.f, 1.25f / mm_per_px));
  for (size_t i = 0; i < mean.v.size(); ++i) tooth.v[i] -= mean.v[i];
}

Spectrum Canvas::reflectance(int x, int y) const {
  const PigVec& p = pig(x, y);
  Spectrum kd, sd;
  if (overlay) {
    PigVec t = p;
    const PigVec& o = (*overlay)(x, y);
    for (int i = 0; i < kMaxPig; ++i) t[i] += o[i];
    palette.film(t, kd, sd);
  } else {
    palette.film(p, kd, sd);
  }
  return km_over(kd, sd, base(x, y));
}

void Canvas::dry(float shrink) {
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < w; ++x) {
        float v = vol(x, y);
        const PigVec& p = pig(x, y);
        bool any = v > 0.f;
        for (int i = 0; i < palette.n && !any; ++i) any = p[i] > 0.f;
        if (!any) continue;
        base(x, y) = reflectance(x, y);
        dry_h(x, y) += film_relief * v * shrink;
        pig(x, y) = PigVec{};
        vol(x, y) = 0.f;
      }
  }, 8);
  refresh_tooth();
  mark_dirty({0, 0, w, h});
}

void Canvas::render_albedo(const Rect& r_in, Image& out) const {
  Rect r = r_in.clipped(w, h);
  if (r.empty()) return;
  auto body = [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = r.x0; x < r.x1; ++x) out(x, y) = albedo(x, y);
  };
  if (r.width() * r.height() > 20000) parallel_for(r.y0, r.y1, body, 4);
  else body(r.y0, r.y1);
}

Image Canvas::render(const Light& lt) const {
  Image out(w, h);
  Vec2 ld = normalize(lt.dir);
  float ce = std::cos(lt.elevation), se = std::sin(lt.elevation);
  // Unit vector toward the light, and the half vector for a viewer overhead.
  float lx = ld.x * ce, ly = ld.y * ce, lz = se;
  float hx = lx, hy = ly, hz = lz + 1.f;
  float hl = std::sqrt(hx * hx + hy * hy + hz * hz);
  hx /= hl; hy /= hl; hz /= hl;
  const float shininess = 28.f;
  float flat_spec = std::pow(hz, shininess);
  float slope_scale = lt.relief_gain / mm_per_px;
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < w; ++x) {
        int xm = std::max(x - 1, 0), xp = std::min(x + 1, w - 1);
        int ym = std::max(y - 1, 0), yp = std::min(y + 1, h - 1);
        float dx = (top(xp, y) - top(xm, y)) * 0.5f * slope_scale;
        float dy = (top(x, yp) - top(x, ym)) * 0.5f * slope_scale;
        float nl = 1.f / std::sqrt(dx * dx + dy * dy + 1.f);
        float nx = -dx * nl, ny = -dy * nl, nz = nl;
        float diffuse = std::max(nx * lx + ny * ly + nz * lz, 0.f) / lz;
        float shade = lt.ambient + (1.f - lt.ambient) * diffuse;
        Rgb c = albedo(x, y) * shade;
        if (gloss > 0.f) {
          float g = gloss * std::min(1.f, vol(x, y) / gloss_thickness + dry_h(x, y) / gloss_thickness);
          float nh = std::max(nx * hx + ny * hy + nz * hz, 0.f);
          float spec = g * std::max(std::pow(nh, shininess) - flat_spec, 0.f);
          c = c + Rgb{spec, spec, spec};
        }
        out(x, y) = c;
      }
  }, 4);
  return out;
}

}  // namespace bs
