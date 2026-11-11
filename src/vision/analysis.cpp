#include "vision/analysis.hpp"

namespace bs {
namespace {

struct Lab3 {
  float L = 0.f, a = 0.f, b = 0.f;
  Lab3 operator+(Lab3 o) const { return {L + o.L, a + o.a, b + o.b}; }
  Lab3 operator*(float s) const { return {L * s, a * s, b * s}; }
};

struct Tensor {
  float xx = 0.f, xy = 0.f, yy = 0.f;
  Tensor operator+(Tensor o) const { return {xx + o.xx, xy + o.xy, yy + o.yy}; }
  Tensor operator*(float s) const { return {xx * s, xy * s, yy * s}; }
};

Plane<Lab3> to_lab(const Image& img) {
  Plane<Lab3> lab(img.w, img.h);
  parallel_for(0, img.h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < img.w; ++x) {
        Lab l = rgb_to_oklab(img(x, y));
        lab(x, y) = {l.L, l.a, l.b};
      }
  }, 8);
  return lab;
}

// Scales a map so its 97th percentile maps to 1.
void normalize_percentile(Plane<float>& p, float pct = 0.97f) {
  std::vector<float> s;
  size_t stride = std::max<size_t>(1, p.v.size() / 40000);
  for (size_t i = 0; i < p.v.size(); i += stride) s.push_back(p.v[i]);
  if (s.empty()) return;
  size_t k = static_cast<size_t>(pct * (s.size() - 1));
  std::nth_element(s.begin(), s.begin() + k, s.end());
  float ref = std::max(s[k], 1e-6f);
  for (float& v : p.v) v = clamp01(v / ref);
}

}  // namespace

FlowField compute_flow(const Image& img, float sigma_tensor) {
  const int w = img.w, h = img.h;
  Plane<Lab3> lab = to_lab(img);
  Plane<Tensor> J(w, h);
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < w; ++x) {
        // Sobel derivative of each channel.
        Tensor t;
        auto g = [&](int dx, int dy) -> const Lab3& { return lab.clamped(x + dx, y + dy); };
        const Lab3 &a = g(-1, -1), &b = g(0, -1), &c = g(1, -1), &d = g(-1, 0), &f = g(1, 0),
                   &p = g(-1, 1), &q = g(0, 1), &r = g(1, 1);
        float gxL = (c.L + 2 * f.L + r.L) - (a.L + 2 * d.L + p.L);
        float gyL = (p.L + 2 * q.L + r.L) - (a.L + 2 * b.L + c.L);
        float gxa = (c.a + 2 * f.a + r.a) - (a.a + 2 * d.a + p.a);
        float gya = (p.a + 2 * q.a + r.a) - (a.a + 2 * b.a + c.a);
        float gxb = (c.b + 2 * f.b + r.b) - (a.b + 2 * d.b + p.b);
        float gyb = (p.b + 2 * q.b + r.b) - (a.b + 2 * b.b + c.b);
        t.xx = gxL * gxL + gxa * gxa + gxb * gxb;
        t.xy = gxL * gyL + gxa * gya + gxb * gyb;
        t.yy = gyL * gyL + gya * gya + gyb * gyb;
        J(x, y) = t;
      }
  }, 8);
  gaussian_blur(J, sigma_tensor);
  FlowField ff;
  ff.dir = Plane<Vec2>(w, h);
  ff.coherence = Plane<float>(w, h);
  ff.strength = Plane<float>(w, h);
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < w; ++x) {
        const Tensor& t = J(x, y);
        // Eigen-decomposition of the 2x2 tensor. The major eigenvector points
        // across the edge; the stroke direction is perpendicular to it.
        float tr = t.xx + t.yy, df = t.xx - t.yy;
        float root = std::sqrt(df * df + 4.f * t.xy * t.xy);
        float l1 = 0.5f * (tr + root), l2 = 0.5f * (tr - root);
        Vec2 major = std::abs(t.xy) > 1e-12f ? Vec2{l1 - t.yy, t.xy} : (t.xx >= t.yy ? Vec2{1, 0} : Vec2{0, 1});
        ff.dir(x, y) = perp(normalize(major));
        ff.coherence(x, y) = tr > 1e-9f ? (l1 - l2) / (l1 + l2) : 0.f;
        ff.strength(x, y) = std::sqrt(std::max(tr, 0.f));
      }
  }, 8);
  normalize_percentile(ff.strength, 0.9f);
  return ff;
}

Subject observe(const Image& photo, int w, int h) {
  Subject s;
  s.color = resize_area(photo, w, h);
  s.light = Plane<float>(w, h);
  Plane<Lab3> lab = to_lab(s.color);
  for (size_t i = 0; i < lab.v.size(); ++i) s.light.v[i] = clamp01(lab.v[i].L);

  // Edge strength: color gradient magnitude at a fine scale.
  s.edge = Plane<float>(w, h);
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < w; ++x) {
        const Lab3 &l = lab.clamped(x - 1, y), &r = lab.clamped(x + 1, y);
        const Lab3 &u = lab.clamped(x, y - 1), &d = lab.clamped(x, y + 1);
        float gx2 = sqr(r.L - l.L) + sqr(r.a - l.a) + sqr(r.b - l.b);
        float gy2 = sqr(d.L - u.L) + sqr(d.a - u.a) + sqr(d.b - u.b);
        s.edge(x, y) = std::sqrt(gx2 + gy2);
      }
  }, 8);
  gaussian_blur(s.edge, 1.0f);

  // Detail: how much fine structure surrounds a point. Pooled edge energy
  // over a neighborhood about 1.5% of the picture, so busy regions (faces,
  // foliage, lettering) stand out from smooth ones (sky, walls).
  s.detail = s.edge;
  float pool = std::max(2.f, 0.012f * std::max(w, h));
  gaussian_blur(s.detail, pool);
  normalize_percentile(s.detail, 0.95f);
  normalize_percentile(s.edge, 0.97f);
  return s;
}

Image block_mean(const Image& a, int block) {
  int bw = (a.w + block - 1) / block, bh = (a.h + block - 1) / block;
  Image out(bw, bh);
  parallel_for(0, bh, [&](int lo, int hi) {
    for (int by = lo; by < hi; ++by)
      for (int bx = 0; bx < bw; ++bx) {
        Rgb acc;
        int n = 0;
        for (int y = by * block; y < std::min((by + 1) * block, a.h); ++y)
          for (int x = bx * block; x < std::min((bx + 1) * block, a.w); ++x) {
            acc += a(x, y);
            ++n;
          }
        out(bx, by) = acc * (1.f / std::max(n, 1));
      }
  }, 2);
  return out;
}

Plane<float> block_difference(const Image& a, const Image& b, int block) {
  Image ma = block_mean(a, block), mb = block_mean(b, block);
  Plane<float> d(ma.w, ma.h);
  for (size_t i = 0; i < d.v.size(); ++i)
    d.v[i] = lab_dist(rgb_to_oklab(ma.v[i]), rgb_to_oklab(mb.v[i]));
  return d;
}

}  // namespace bs
