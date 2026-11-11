#include "optics/mixer.hpp"

namespace bs {
namespace {

void normalize_frac(PigVec& f) {
  float s = sum(f);
  if (s > 0.f)
    for (float& v : f) v /= s;
}

}  // namespace

Lab Mixer::opaque_lab(const PigVec& frac) const {
  return rgb_to_oklab(spectrum_to_rgb(pal_.masstone(frac)));
}

Lab Mixer::glaze_lab(const Spectrum& base, const PigVec& amount) const {
  Spectrum kd, sd;
  pal_.film(amount, kd, sd);
  return rgb_to_oklab(spectrum_to_rgb(km_over(kd, sd, base)));
}

Mixer::Mixer(const Palette& palette) : pal_(palette) {
  // Colored (non-white) pigments.
  std::vector<int> col;
  for (int i = 0; i < pal_.n; ++i)
    if (i != pal_.white) col.push_back(i);
  const int nc = static_cast<int>(col.size());

  // Chromatic mixes the way painters actually mix: one, two or three tube
  // colors. Pigments differ in tinting strength by orders of magnitude, so
  // proportions are spaced geometrically.
  std::vector<PigVec> mixes;
  for (int a = 0; a < nc; ++a) {
    PigVec f{};
    f[col[a]] = 1.f;
    mixes.push_back(f);
  }
  const float ratios[] = {1.f / 16, 1.f / 8, 1.f / 4, 1.f / 2, 1.f, 2.f, 4.f, 8.f, 16.f};
  for (int a = 0; a < nc; ++a)
    for (int b = a + 1; b < nc; ++b)
      for (float r : ratios) {
        PigVec f{};
        f[col[a]] = 1.f;
        f[col[b]] = r;
        normalize_frac(f);
        mixes.push_back(f);
      }
  const size_t pair_end = mixes.size();
  const float w3[] = {1.f, 3.f, 9.f};
  for (int a = 0; a < nc; ++a)
    for (int b = a + 1; b < nc; ++b)
      for (int c = b + 1; c < nc; ++c)
        for (float wa : w3)
          for (float wb : w3)
            for (float wc : w3) {
              if (wa == wb && wb == wc && wa != 1.f) continue;
              PigVec f{};
              f[col[a]] = wa; f[col[b]] = wb; f[col[c]] = wc;
              normalize_frac(f);
              mixes.push_back(f);
            }

  // Opaque candidates: each chromatic mix at a ladder of white additions.
  if (pal_.white >= 0) {
    const float white_ratio[] = {0.f, 0.125f, 0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f,
                                 16.f, 32.f, 64.f, 128.f, 256.f, 600.f};
    for (const PigVec& m : mixes)
      for (float wr : white_ratio) {
        Candidate c;
        c.frac = m;
        c.frac[pal_.white] = wr;
        normalize_frac(c.frac);
        c.lab = opaque_lab(c.frac);
        opaque_.push_back(c);
      }
    Candidate w;
    w.frac = PigVec{};
    w.frac[pal_.white] = 1.f;
    w.lab = opaque_lab(w.frac);
    opaque_.push_back(w);
  } else {
    for (const PigVec& m : mixes) opaque_.push_back({m, opaque_lab(m)});
  }

  // Glaze search uses single pigments and pairs only (evaluated per query).
  for (size_t i = 0; i < pair_end; ++i) {
    Chroma c;
    c.frac = mixes[i];
    pal_.film(c.frac, c.k, c.s);
    chroma_.push_back(c);
  }
}

PigVec Mixer::match_opaque(Rgb target) const {
  auto q = [](float v) { return static_cast<uint32_t>(clamp01(std::sqrt(clamp01(v))) * 127.f + 0.5f); };
  uint32_t key = (q(target.r) << 14) | (q(target.g) << 7) | q(target.b);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;

  Lab want = rgb_to_oklab(target);
  float best = 1e30f;
  const Candidate* bc = &opaque_.front();
  for (const Candidate& c : opaque_) {
    float d = lab_dist2(c.lab, want);
    if (d < best) { best = d; bc = &c; }
  }
  // Local refinement: nudge each tube's share up or down, as one adjusts a
  // mix on the palette.
  PigVec f = bc->frac;
  float step = 1.35f;
  for (int iter = 0; iter < 6; ++iter) {
    for (int i = 0; i < pal_.n; ++i) {
      if (f[i] <= 0.f) continue;
      for (float m : {step, 1.f / step}) {
        PigVec t = f;
        t[i] *= m;
        normalize_frac(t);
        float d = lab_dist2(opaque_lab(t), want);
        if (d < best) { best = d; f = t; }
      }
    }
    step = 1.f + (step - 1.f) * 0.6f;
  }
  cache_.emplace(key, f);
  return f;
}

PigVec Mixer::match_glaze(const Spectrum& base, Rgb target, float max_total) const {
  Lab want = rgb_to_oklab(target);
  PigVec best_amt{};
  float best = lab_dist2(rgb_to_oklab(spectrum_to_rgb(base)), want);  // doing nothing
  const int levels = 11;
  for (const Chroma& c : chroma_) {
    for (int l = 0; l < levels; ++l) {
      float amt = max_total * std::pow(0.62f, static_cast<float>(levels - 1 - l));
      Spectrum kd, sd;
      for (int b = 0; b < kBands; ++b) { kd[b] = c.k[b] * amt; sd[b] = c.s[b] * amt; }
      float d = lab_dist2(rgb_to_oklab(spectrum_to_rgb(km_over(kd, sd, base))), want);
      if (d < best) {
        best = d;
        for (int i = 0; i < kMaxPig; ++i) best_amt[i] = c.frac[i] * amt;
      }
    }
  }
  if (sum(best_amt) <= 0.f) return best_amt;
  float step = 1.3f;
  for (int iter = 0; iter < 6; ++iter) {
    for (int i = 0; i < pal_.n; ++i) {
      if (best_amt[i] <= 0.f) continue;
      for (float m : {step, 1.f / step}) {
        PigVec t = best_amt;
        t[i] *= m;
        if (sum(t) > max_total) continue;
        float d = lab_dist2(glaze_lab(base, t), want);
        if (d < best) { best = d; best_amt = t; }
      }
    }
    step = 1.f + (step - 1.f) * 0.6f;
  }
  return best_amt;
}

}  // namespace bs
