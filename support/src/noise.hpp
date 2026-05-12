#pragma once

// Tiny self-contained value noise + FBM. Kept here (not in a public
// header) because the only consumer is generate.cpp; if a second one
// appears, promote to include/.

#include <cmath>
#include <cstdint>

namespace broadstroke::support::detail {

// Wang/Jenkins-style integer hash. Cheap and good enough for value noise.
inline std::uint32_t hash3(std::int32_t x, std::int32_t y, std::uint64_t seed) {
  std::uint32_t h = static_cast<std::uint32_t>(seed) ^ 0x9E3779B9u;
  h ^= static_cast<std::uint32_t>(x) * 0x85EBCA6Bu;
  h = (h ^ (h >> 13)) * 0xC2B2AE35u;
  h ^= static_cast<std::uint32_t>(y) * 0x27D4EB2Fu;
  h = (h ^ (h >> 16));
  return h;
}

inline float hashToUnit(std::int32_t x, std::int32_t y, std::uint64_t seed) {
  return hash3(x, y, seed) * (1.0f / 4294967296.0f);  // [0, 1)
}

inline float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

// 2D value noise sampled at fractional (x, y), output in [0, 1).
inline float valueNoise(float x, float y, std::uint64_t seed) {
  const std::int32_t xi = static_cast<std::int32_t>(std::floor(x));
  const std::int32_t yi = static_cast<std::int32_t>(std::floor(y));
  const float fx = x - static_cast<float>(xi);
  const float fy = y - static_cast<float>(yi);

  const float v00 = hashToUnit(xi,     yi,     seed);
  const float v10 = hashToUnit(xi + 1, yi,     seed);
  const float v01 = hashToUnit(xi,     yi + 1, seed);
  const float v11 = hashToUnit(xi + 1, yi + 1, seed);

  const float sx = smoothstep(fx);
  const float sy = smoothstep(fy);
  const float a = v00 + (v10 - v00) * sx;
  const float b = v01 + (v11 - v01) * sx;
  return a + (b - a) * sy;
}

// Fractional Brownian motion. Returns sum of octaves with the given
// persistence (amplitude decay) and lacunarity (frequency growth),
// normalized so the result is in [0, 1).
inline float fbm(float x, float y,
                 int octaves, float persistence, float lacunarity,
                 std::uint64_t seed) {
  float amp = 1.0f;
  float freq = 1.0f;
  float sum = 0.0f;
  float norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum  += amp * valueNoise(x * freq, y * freq, seed + static_cast<std::uint64_t>(i) * 1013u);
    norm += amp;
    amp  *= persistence;
    freq *= lacunarity;
  }
  return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

}  // namespace broadstroke::support::detail
