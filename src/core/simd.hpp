// Eight floats at a time (AVX).
//
// A pigment vector or a spectrum is eight floats, and several hot loops run
// over many cells doing the same arithmetic. These helpers keep such code
// readable. Additions, subtractions, multiplications and divisions are done
// lane by lane exactly as a scalar loop would do them (nothing is fused or
// reordered); exp8 and log8 are accurate to about one unit in the last place
// but do not reproduce the C library's results bit for bit.
#pragma once

#include <array>

#include <immintrin.h>

namespace bs {

using Vec8 = __m256;
inline Vec8 load8(const std::array<float, 8>& a) { return _mm256_loadu_ps(a.data()); }
inline void store8(std::array<float, 8>& a, Vec8 v) { _mm256_storeu_ps(a.data(), v); }
inline Vec8 splat8(float v) { return _mm256_set1_ps(v); }

// Sum of the eight lanes, always added in the same order.
inline float sum8(Vec8 v) {
  __m128 lo = _mm256_castps256_ps128(v), hi = _mm256_extractf128_ps(v, 1);
  __m128 s = _mm_add_ps(lo, hi);
  s = _mm_add_ps(s, _mm_movehl_ps(s, s));
  s = _mm_add_ss(s, _mm_shuffle_ps(s, s, 1));
  return _mm_cvtss_f32(s);
}

// exp(x): argument reduction by powers of two and a degree-5 polynomial (the
// Cephes single-precision method). exp8(0) is exactly 1.
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

// log(x) for positive, normal x: the Cephes single-precision method (split
// into mantissa and exponent, then a degree-8 polynomial). log8(1) is exactly 0.
inline Vec8 log8(Vec8 x) {
  __m256i bits = _mm256_castps_si256(x);
  __m256i exponent = _mm256_sub_epi32(_mm256_srli_epi32(bits, 23), _mm256_set1_epi32(126));
  // Mantissa in [0.5, 1).
  Vec8 m = _mm256_or_ps(_mm256_and_ps(x, _mm256_castsi256_ps(_mm256_set1_epi32(0x807fffff))), splat8(0.5f));
  Vec8 e = _mm256_cvtepi32_ps(exponent);
  // Below sqrt(1/2) use 2m and one less in the exponent, so m - 1 stays small.
  Vec8 low = _mm256_cmp_ps(m, splat8(0.707106781186547524f), _CMP_LT_OQ);
  Vec8 extra = _mm256_and_ps(m, low);
  m = _mm256_sub_ps(m, splat8(1.f));
  e = _mm256_sub_ps(e, _mm256_and_ps(splat8(1.f), low));
  m = _mm256_add_ps(m, extra);
  Vec8 z = _mm256_mul_ps(m, m);
  Vec8 y = splat8(7.0376836292e-2f);
  y = _mm256_add_ps(_mm256_mul_ps(y, m), splat8(-1.1514610310e-1f));
  y = _mm256_add_ps(_mm256_mul_ps(y, m), splat8(1.1676998740e-1f));
  y = _mm256_add_ps(_mm256_mul_ps(y, m), splat8(-1.2420140846e-1f));
  y = _mm256_add_ps(_mm256_mul_ps(y, m), splat8(1.4249322787e-1f));
  y = _mm256_add_ps(_mm256_mul_ps(y, m), splat8(-1.6668057665e-1f));
  y = _mm256_add_ps(_mm256_mul_ps(y, m), splat8(2.0000714765e-1f));
  y = _mm256_add_ps(_mm256_mul_ps(y, m), splat8(-2.4999993993e-1f));
  y = _mm256_add_ps(_mm256_mul_ps(y, m), splat8(3.3333331174e-1f));
  y = _mm256_mul_ps(_mm256_mul_ps(y, m), z);
  y = _mm256_add_ps(y, _mm256_mul_ps(e, splat8(-2.12194440e-4f)));
  y = _mm256_sub_ps(y, _mm256_mul_ps(z, splat8(0.5f)));
  Vec8 r = _mm256_add_ps(m, y);
  return _mm256_add_ps(r, _mm256_mul_ps(e, splat8(0.693359375f)));
}

}  // namespace bs
