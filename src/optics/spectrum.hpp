// Spectral representation and Kubelka-Munk film optics.
//
// Color in the engine is never RGB arithmetic. Every material on the canvas
// is a film of pigments, each with a wavelength-dependent absorption K and
// scattering S; what the eye sees is the Kubelka-Munk reflectance of those
// films over whatever lies beneath, integrated against the CIE observer under
// D65. That is what makes blue + yellow mix to green, makes glazes darken
// multiplicatively, and lets thin paint show the ground through it.
#pragma once

#include <immintrin.h>

#include "core/common.hpp"

namespace bs {

// Spectral bands: kBands equal slices of 400-700 nm.
constexpr int kBands = 8;
constexpr float kLambdaMin = 400.f;
constexpr float kLambdaMax = 700.f;
// Maximum number of pigments in a working palette.
constexpr int kMaxPig = 8;

using Spectrum = std::array<float, kBands>;
using PigVec = std::array<float, kMaxPig>;

// A pigment vector or a spectrum is eight floats: one AVX register. The hot
// loops over them are written with these helpers. Every lane is computed
// with the same separate multiply, add and subtract a scalar loop would use
// (nothing is fused or reordered), so the results are bit-for-bit those of
// the scalar code.
static_assert(kBands == 8 && kMaxPig == 8, "the eight-lane helpers assume eight bands and eight pigments");
using Vec8 = __m256;
inline Vec8 load8(const std::array<float, 8>& a) { return _mm256_loadu_ps(a.data()); }
inline void store8(std::array<float, 8>& a, Vec8 v) { _mm256_storeu_ps(a.data(), v); }
inline Vec8 splat8(float v) { return _mm256_set1_ps(v); }

// exp(x) for eight values at once: argument reduction by powers of two and a
// degree-5 polynomial (the Cephes single-precision method). Accurate to
// about one unit in the last place, and exp8(0) is exactly 1.
inline Vec8 exp8(Vec8 x) {
  x = _mm256_min_ps(x, splat8(88.3762626647949f));
  x = _mm256_max_ps(x, splat8(-88.3762626647949f));
  Vec8 n = _mm256_floor_ps(_mm256_add_ps(_mm256_mul_ps(x, splat8(1.44269504088896341f)), splat8(0.5f)));
  x = _mm256_sub_ps(x, _mm256_mul_ps(n, splat8(0.693359375f)));
  x = _mm256_sub_ps(x, _mm256_mul_ps(n, splat8(-2.12194440e-4f)));
  Vec8 y = splat8(1.9875691500e-4f);
  y = _mm256_add_ps(_mm256_mul_ps(y, x), splat8(1.3981999507e-3f));
  y = _mm256_add_ps(_mm256_mul_ps(y, x), splat8(8.3334519073e-3f));
  y = _mm256_add_ps(_mm256_mul_ps(y, x), splat8(4.1665795894e-2f));
  y = _mm256_add_ps(_mm256_mul_ps(y, x), splat8(1.6666665459e-1f));
  y = _mm256_add_ps(_mm256_mul_ps(y, x), splat8(5.0000001201e-1f));
  y = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(y, _mm256_mul_ps(x, x)), x), splat8(1.f));
  __m256i e = _mm256_slli_epi32(_mm256_add_epi32(_mm256_cvttps_epi32(n), _mm256_set1_epi32(127)), 23);
  return _mm256_mul_ps(y, _mm256_castsi256_ps(e));
}

inline Spectrum spectrum_fill(float v) {
  Spectrum s;
  s.fill(v);
  return s;
}
inline float sum(const PigVec& p) {
  float s = 0.f;
  for (float v : p) s += v;
  return s;
}

// Reflectance of a pigmented film over a background, per band (Kubelka-Munk
// two-flux solution for a finite layer):
//   R = (1 - Rg (a - b coth(b S d))) / (a - Rg + b coth(b S d)),
//   a = 1 + K/S,  b = sqrt(a^2 - 1).
// Kd and Sd are the film's absorption and scattering already multiplied by
// its thickness. Limits: d -> 0 gives Rg; d -> inf gives the masstone
// R_inf = a - b; S -> 0 gives pure absorption Rg exp(-2 K d).
inline float km_over_band(float kd, float sd, float rg) {
  if (kd + sd < 1e-7f) return rg;
  if (sd < 1e-4f * kd + 1e-7f) return rg * std::exp(-2.f * kd);
  float a = 1.f + kd / sd;
  float b = std::sqrt(a * a - 1.f);
  float x = b * sd;
  if (x > 12.f) return a - b;  // optically thick: masstone
  // b coth(x). For a very thin film exp(-2x) rounds to 1, so (1 + e) / (1 - e)
  // loses its digits and finally divides by zero. There the series of coth
  // is used instead: b coth(x) = b / x + b x / 3 - ... = 1 / Sd + b^2 Sd / 3.
  float bc;
  if (x < 1e-3f) {
    bc = 1.f / sd + b * b * sd * (1.f / 3.f);
  } else {
    float e = std::exp(-2.f * x);
    bc = b * ((1.f + e) / (1.f - e));
  }
  return (1.f - rg * (a - bc)) / (a - rg + bc);
}

// The same for all eight bands at once. Every case of km_over_band is
// computed for every band and the right one selected, so a band that does
// not use a quantity may briefly hold a meaningless value for it.
inline Spectrum km_over(const Spectrum& kd_in, const Spectrum& sd_in, const Spectrum& rg_in) {
  const Vec8 kd = load8(kd_in), sd = load8(sd_in), rg = load8(rg_in);
  const Vec8 one = splat8(1.f);
  const Vec8 none = _mm256_cmp_ps(_mm256_add_ps(kd, sd), splat8(1e-7f), _CMP_LT_OQ);  // no film
  const Vec8 absorb =  // no scattering to speak of: Beer-Lambert over two passes
      _mm256_cmp_ps(sd, _mm256_add_ps(_mm256_mul_ps(splat8(1e-4f), kd), splat8(1e-7f)), _CMP_LT_OQ);
  const Vec8 s = _mm256_blendv_ps(sd, one, _mm256_or_ps(none, absorb));  // keeps the division finite where unused
  const Vec8 a = _mm256_add_ps(one, _mm256_div_ps(kd, s));
  const Vec8 b = _mm256_sqrt_ps(_mm256_sub_ps(_mm256_mul_ps(a, a), one));
  const Vec8 x = _mm256_mul_ps(b, s);
  const Vec8 thick = _mm256_cmp_ps(x, splat8(12.f), _CMP_GT_OQ);  // optically thick: masstone
  const Vec8 thin = _mm256_cmp_ps(x, splat8(1e-3f), _CMP_LT_OQ);  // series of coth, see km_over_band
  const Vec8 e = exp8(_mm256_mul_ps(splat8(-2.f), _mm256_blendv_ps(x, kd, absorb)));
  const Vec8 bc_general = _mm256_mul_ps(b, _mm256_div_ps(_mm256_add_ps(one, e), _mm256_sub_ps(one, e)));
  const Vec8 bc_thin = _mm256_add_ps(_mm256_div_ps(one, s),
                                     _mm256_mul_ps(_mm256_mul_ps(_mm256_mul_ps(b, b), s), splat8(1.f / 3.f)));
  const Vec8 bc = _mm256_blendv_ps(bc_general, bc_thin, thin);
  Vec8 r = _mm256_div_ps(_mm256_sub_ps(one, _mm256_mul_ps(rg, _mm256_sub_ps(a, bc))),
                         _mm256_add_ps(_mm256_sub_ps(a, rg), bc));
  r = _mm256_blendv_ps(r, _mm256_sub_ps(a, b), thick);
  r = _mm256_blendv_ps(r, _mm256_mul_ps(rg, e), absorb);
  r = _mm256_blendv_ps(r, rg, none);
  Spectrum out;
  store8(out, r);
  return out;
}

// Masstone (infinitely thick film) reflectance from K/S.
inline float km_masstone_band(float k, float s) {
  if (s < 1e-9f) return 0.f;
  float q = k / s;
  return 1.f + q - std::sqrt(q * q + 2.f * q);
}

// Perceptual color space used for every color comparison the artist makes.
struct Lab {
  float L = 0.f, a = 0.f, b = 0.f;
};
inline Lab rgb_to_oklab(Rgb c) {
  float r = std::max(c.r, 0.f), g = std::max(c.g, 0.f), b = std::max(c.b, 0.f);
  float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
  float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
  float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
  return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
          1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
          0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}
inline Rgb oklab_to_rgb(Lab c) {
  float l = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
  float m = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
  float s = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
  l = l * l * l; m = m * m * m; s = s * s * s;
  return {std::max(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s, 0.f),
          std::max(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s, 0.f),
          std::max(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s, 0.f)};
}
inline float lab_dist2(Lab p, Lab q) {
  return sqr(p.L - q.L) + sqr(p.a - q.a) + sqr(p.b - q.b);
}
inline float lab_dist(Lab p, Lab q) { return std::sqrt(lab_dist2(p, q)); }

// Linear sRGB of a reflectance spectrum viewed under D65.
Rgb spectrum_to_rgb(const Spectrum& reflectance);

// A smooth reflectance spectrum that reproduces the given linear RGB (used
// for paper and ground colors, which are specified as colors, not pigments).
Spectrum rgb_to_spectrum(Rgb c);

}  // namespace bs
