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

// How an item's importance changes the care the artist takes there. The
// detail map is the artist's measure of care (a higher value means a lower
// tolerance for error and finer work). It is scaled by `gain` and raised to
// at least `floor`:
//   - the main subject and its key parts get close attention even where
//     they are smooth (a cheek has little edge energy but matters most);
//   - the background gets less than its busyness alone would earn.
// Pixels that belong to no item keep the detail they had.
struct Care {
  float gain, floor;
};
constexpr Care kCare[4] = {
    {0.40f, 0.00f},  // 0: background
    {0.75f, 0.00f},  // 1: supporting
    {1.00f, 0.35f},  // 2: matters to the picture
    {1.00f, 0.70f},  // 3: the main subject and its key parts
};

// The scene's item layer on the canvas grid: for each cell, the layer pixel
// nearest its center, found in whole numbers.
Plane<uint8_t> item_map(const Scene& scene, int w, int h) {
  Plane<uint8_t> m(w, h);
  const long long lw = scene.item.w, lh = scene.item.h;
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y) {
      int ly = static_cast<int>(std::min(lh - 1, (2LL * y + 1) * lh / (2LL * h)));
      for (int x = 0; x < w; ++x) {
        int lx = static_cast<int>(std::min(lw - 1, (2LL * x + 1) * lw / (2LL * w)));
        m(x, y) = scene.item(lx, ly);
      }
    }
  }, 8);
  return m;
}

// Squared distance from each position on a line to the nearest position
// where f is zero, as the lower envelope of parabolas (Felzenszwalb and
// Huttenlocher). v and z are scratch of n and n + 1 entries.
void distance_1d(const double* f, int n, double* d, int* v, double* z) {
  int k = 0;
  v[0] = 0;
  z[0] = -1e30;
  z[1] = 1e30;
  for (int q = 1; q < n; ++q) {
    double s;
    for (;;) {
      int p = v[k];
      s = ((f[q] + static_cast<double>(q) * q) - (f[p] + static_cast<double>(p) * p)) / (2.0 * q - 2.0 * p);
      if (s > z[k]) break;
      --k;
    }
    ++k;
    v[k] = q;
    z[k] = s;
    z[k + 1] = 1e30;
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[k + 1] < q) ++k;
    int p = v[k];
    d[q] = static_cast<double>(q - p) * (q - p) + f[p];
  }
}

void apply_importance(Subject& s, const Scene& scene) {
  const int w = s.detail.w, h = s.detail.h;
  Care by_id[256];
  for (Care& c : by_id) c = {1.f, 0.f};
  for (const SceneItem& it : scene.items) by_id[it.id] = kCare[std::clamp(it.importance, 0, 3)];

  Plane<float> gain(w, h), floor(w, h);
  for (size_t i = 0; i < s.item.v.size(); ++i) {
    const Care& c = by_id[s.item.v[i]];
    gain.v[i] = c.gain;
    floor.v[i] = c.floor;
  }
  // An item's outline is not a line the artist's care stops at: let it
  // change over about half the distance detail itself is pooled over.
  float soften = std::max(1.f, 0.006f * std::max(w, h));
  gaussian_blur(gain, soften);
  gaussian_blur(floor, soften);
  for (size_t i = 0; i < s.detail.v.size(); ++i)
    s.detail.v[i] = clamp01(std::max(floor.v[i], gain.v[i] * s.detail.v[i]));
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

Subject observe(const Image& photo, int w, int h, const Scene* scene) {
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
  if (scene && !scene->empty()) {
    s.item = item_map(*scene, w, h);
    s.treatment.assign(256, std::string());
    for (const SceneItem& it : scene->items) s.treatment[it.id] = it.treatment;
    if (!scene->normal.empty()) {
      Image normal = resize_area(scene->normal, w, h);
      for (Rgb& n : normal.v) {
        float len = std::sqrt(n.r * n.r + n.g * n.g + n.b * n.b);
        n = len > 1e-3f ? n * (1.f / len) : Rgb{0.f, 0.f, 1.f};
      }
      s.form = form_flow(normal, s.item);
    }
    apply_importance(s, *scene);
  }
  return s;
}

Outlines find_outlines(const Plane<uint8_t>& item) {
  const int w = item.w, h = item.h;
  Outlines o;
  o.dist = Plane<float>(w, h);
  o.along = Plane<Vec2>(w, h, Vec2{1.f, 0.f});
  if (item.empty()) return o;
  // A cell is on an outline if a neighbor belongs to another item.
  const double kFar = 1e12;
  Plane<double> d2(w, h);
  parallel_for(0, w, [&](int lo, int hi) {
    std::vector<double> f(h), d(h), z(h + 1);
    std::vector<int> v(h);
    for (int x = lo; x < hi; ++x) {
      for (int y = 0; y < h; ++y) {
        uint8_t id = item(x, y);
        bool on = item.clamped(x - 1, y) != id || item.clamped(x + 1, y) != id || item.clamped(x, y - 1) != id ||
                  item.clamped(x, y + 1) != id;
        f[y] = on ? 0.0 : kFar;
      }
      distance_1d(f.data(), h, d.data(), v.data(), z.data());
      for (int y = 0; y < h; ++y) d2(x, y) = d[y];
    }
  }, 8);
  parallel_for(0, h, [&](int lo, int hi) {
    std::vector<double> f(w), d(w), z(w + 1);
    std::vector<int> v(w);
    for (int y = lo; y < hi; ++y) {
      for (int x = 0; x < w; ++x) f[x] = d2(x, y);
      distance_1d(f.data(), w, d.data(), v.data(), z.data());
      for (int x = 0; x < w; ++x) o.dist(x, y) = static_cast<float>(std::sqrt(std::min(d[x], kFar)));
    }
  }, 8);
  // The outline's direction: across it the distance changes fastest. The
  // gradient points away from the outline on both sides, so it is pooled as
  // a tensor, which has no sign.
  Plane<Tensor> J(w, h);
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < w; ++x) {
        float gx = std::min(o.dist.clamped(x + 1, y), 1e4f) - std::min(o.dist.clamped(x - 1, y), 1e4f);
        float gy = std::min(o.dist.clamped(x, y + 1), 1e4f) - std::min(o.dist.clamped(x, y - 1), 1e4f);
        J(x, y) = {gx * gx, gx * gy, gy * gy};
      }
  }, 8);
  gaussian_blur(J, 2.f);
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < w; ++x) {
        const Tensor& t = J(x, y);
        float tr = t.xx + t.yy, df = t.xx - t.yy;
        float root = std::sqrt(df * df + 4.f * t.xy * t.xy);
        float l1 = 0.5f * (tr + root);
        Vec2 major = std::abs(t.xy) > 1e-12f ? Vec2{l1 - t.yy, t.xy} : (t.xx >= t.yy ? Vec2{1, 0} : Vec2{0, 1});
        o.along(x, y) = perp(normalize(major));
      }
  }, 8);
  return o;
}

namespace {

// Gaussian blur in which each cell gets the weighted mean of the cells of
// its own item only.
template <class T>
Plane<T> blur_within(const Plane<T>& img, const Plane<uint8_t>& item, float sigma) {
  Plane<T> out = img;
  if (sigma < 0.2f || img.empty() || item.empty()) return out;
  const int w = img.w, h = img.h;
  const int r = std::max(1, static_cast<int>(std::ceil(sigma * 3.f)));
  struct Box {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
  };
  std::vector<Box> box(256);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      Box& b = box[item(x, y)];
      b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y);
      b.x1 = std::max(b.x1, x); b.y1 = std::max(b.y1, y);
    }
  for (int id = 0; id < 256; ++id) {
    const Box& b = box[id];
    if (b.x1 < 0) continue;
    // The item's cells and their weight, blurred in a window around it.
    int x0 = std::max(0, b.x0 - r), y0 = std::max(0, b.y0 - r);
    int x1 = std::min(w - 1, b.x1 + r), y1 = std::min(h - 1, b.y1 + r);
    Plane<T> c(x1 - x0 + 1, y1 - y0 + 1);
    Plane<float> m(c.w, c.h, 0.f);
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x)
        if (item(x, y) == id) {
          c(x - x0, y - y0) = img(x, y);
          m(x - x0, y - y0) = 1.f;
        }
    gaussian_blur(c, sigma);
    gaussian_blur(m, sigma);
    for (int y = b.y0; y <= b.y1; ++y)
      for (int x = b.x0; x <= b.x1; ++x)
        if (item(x, y) == id) out(x, y) = c(x - x0, y - y0) * (1.f / std::max(m(x - x0, y - y0), 1e-6f));
  }
  return out;
}

}  // namespace

Image blur_within_items(const Image& img, const Plane<uint8_t>& item, float sigma) {
  return blur_within(img, item, sigma);
}

FlowField form_flow(const Image& normal, const Plane<uint8_t>& item) {
  const int w = normal.w, h = normal.h;
  FlowField ff;
  ff.dir = Plane<Vec2>(w, h, Vec2{1.f, 0.f});
  ff.coherence = Plane<float>(w, h, 0.f);
  ff.strength = Plane<float>(w, h, 0.f);
  if (normal.empty() || item.w != w || item.h != h) return ff;
  const float span = static_cast<float>(std::max(w, h));
  // Two things say which way a surface runs. Where it is curved, the
  // normal changes most across the form (around a limb) and least along
  // it. Where it is flat but leans away, the normal's lean points up the
  // slope, and the level direction is across that. Both are gathered in one
  // tensor whose major axis points around the form or up the slope. The
  // lean counts as much as the curvature of a form about four times the
  // size of the picture, so it decides only where the surface is nearly
  // flat. Changes of the normal from one item to the next are not
  // curvature and are left out.
  const float lean = 300.f / (span * span);
  Plane<Tensor> J(w, h);
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < w; ++x) {
        const Rgb& n = normal(x, y);
        Tensor t{lean * n.r * n.r, lean * n.r * n.g, lean * n.g * n.g};
        const uint8_t id = item(x, y);
        Rgb gx, gy;
        if (x + 1 < w && item(x + 1, y) == id) gx = normal(x + 1, y) - n;
        if (y + 1 < h && item(x, y + 1) == id) gy = normal(x, y + 1) - n;
        t.xx += gx.r * gx.r + gx.g * gx.g + gx.b * gx.b;
        t.xy += gx.r * gy.r + gx.g * gy.g + gx.b * gy.b;
        t.yy += gy.r * gy.r + gy.g * gy.g + gy.b * gy.b;
        J(x, y) = t;
      }
  }, 8);
  // The broad form, not its small folds: pooled over 2% of the long side,
  // within each item.
  J = blur_within(J, item, std::max(2.f, 0.02f * span));
  parallel_for(0, h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < w; ++x) {
        const Tensor& t = J(x, y);
        float tr = t.xx + t.yy, df = t.xx - t.yy;
        float root = std::sqrt(df * df + 4.f * t.xy * t.xy);
        float l1 = 0.5f * (tr + root), l2 = 0.5f * (tr - root);
        Vec2 major = std::abs(t.xy) > 1e-20f ? Vec2{l1 - t.yy, t.xy} : (t.xx >= t.yy ? Vec2{1, 0} : Vec2{0, 1});
        ff.dir(x, y) = perp(normalize(major));
        ff.coherence(x, y) = tr > 1e-20f ? (l1 - l2) / (l1 + l2) : 0.f;
        // Full strength where the normal would turn by 5 across the picture.
        ff.strength(x, y) = clamp01(std::sqrt(std::max(tr, 0.f)) * span / 5.f);
      }
  }, 8);
  return ff;
}

void guide_by_form(FlowField& flow, const FlowField& form) {
  if (form.dir.empty() || form.dir.w != flow.dir.w || form.dir.h != flow.dir.h) return;
  for (size_t i = 0; i < flow.dir.v.size(); ++i) {
    if (flow.coherence.v[i] > 0.25f && flow.strength.v[i] > 0.12f) continue;  // the photo says which way
    if (form.coherence.v[i] <= 0.25f || form.strength.v[i] <= 0.12f) continue;  // the form does not either
    flow.dir.v[i] = form.dir.v[i];
    flow.coherence.v[i] = form.coherence.v[i];
    flow.strength.v[i] = form.strength.v[i];
  }
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
