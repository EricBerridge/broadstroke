// Small math and utility types shared by every module.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace bs {

constexpr float kPi = 3.14159265358979f;

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float e0, float e1, float x) {
  float t = clamp01((x - e0) / (e1 - e0));
  return t * t * (3.f - 2.f * t);
}
inline float sqr(float v) { return v * v; }

struct Vec2 {
  float x = 0.f, y = 0.f;
  Vec2() = default;
  Vec2(float x_, float y_) : x(x_), y(y_) {}
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }
  Vec2 operator/(float s) const { return {x / s, y / s}; }
  Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
  Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
  Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
};
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline Vec2 normalize(Vec2 a, Vec2 fallback = {1.f, 0.f}) {
  float l = length(a);
  return l > 1e-9f ? a / l : fallback;
}
inline Vec2 perp(Vec2 a) { return {-a.y, a.x}; }
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline Vec2 rotate(Vec2 a, float ang) {
  float c = std::cos(ang), s = std::sin(ang);
  return {a.x * c - a.y * s, a.x * s + a.y * c};
}

// Linear-light RGB triple (sRGB primaries, D65).
struct Rgb {
  float r = 0.f, g = 0.f, b = 0.f;
  Rgb() = default;
  Rgb(float r_, float g_, float b_) : r(r_), g(g_), b(b_) {}
  Rgb operator+(Rgb o) const { return {r + o.r, g + o.g, b + o.b}; }
  Rgb operator-(Rgb o) const { return {r - o.r, g - o.g, b - o.b}; }
  Rgb operator*(float s) const { return {r * s, g * s, b * s}; }
  Rgb operator*(Rgb o) const { return {r * o.r, g * o.g, b * o.b}; }
  Rgb& operator+=(Rgb o) { r += o.r; g += o.g; b += o.b; return *this; }
  Rgb& operator*=(float s) { r *= s; g *= s; b *= s; return *this; }
};
inline Rgb lerp(Rgb a, Rgb b, float t) { return a + (b - a) * t; }
inline float luminance(Rgb c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

// Half-open integer rectangle [x0, x1) x [y0, y1).
struct Rect {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool empty() const { return x1 <= x0 || y1 <= y0; }
  int width() const { return x1 - x0; }
  int height() const { return y1 - y0; }
  void include(int x, int y) {
    if (empty()) { x0 = x; y0 = y; x1 = x + 1; y1 = y + 1; return; }
    x0 = std::min(x0, x); y0 = std::min(y0, y);
    x1 = std::max(x1, x + 1); y1 = std::max(y1, y + 1);
  }
  void include(const Rect& o) {
    if (o.empty()) return;
    if (empty()) { *this = o; return; }
    x0 = std::min(x0, o.x0); y0 = std::min(y0, o.y0);
    x1 = std::max(x1, o.x1); y1 = std::max(y1, o.y1);
  }
  Rect clipped(int w, int h) const {
    return {std::max(x0, 0), std::max(y0, 0), std::min(x1, w), std::min(y1, h)};
  }
  Rect grown(int m) const { return {x0 - m, y0 - m, x1 + m, y1 + m}; }
};

// Deterministic PRNG (PCG32). Every stochastic choice in the engine draws from
// an explicitly seeded Rng, so a given photo + settings always produces the
// same artwork.
class Rng {
 public:
  explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL) { reseed(seed); }
  void reseed(uint64_t seed) {
    state_ = 0; inc_ = (seed << 1u) | 1u;
    next(); state_ += seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL; next();
  }
  uint32_t next() {
    uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = static_cast<uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
  }
  // Uniform in [0, 1).
  float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }
  float range(float a, float b) { return a + (b - a) * uniform(); }
  int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
  // Standard normal (Box-Muller).
  float normal() {
    float u1 = std::max(uniform(), 1e-7f), u2 = uniform();
    return std::sqrt(-2.f * std::log(u1)) * std::cos(2.f * kPi * u2);
  }
 private:
  uint64_t state_ = 0, inc_ = 1;
};

// Cheap integer hash to [0, 1), for lattice noise.
inline float hash01(int x, int y, uint32_t seed) {
  uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u ^ static_cast<uint32_t>(y) * 0xd8163841u ^
               seed * 0xcb1ab31fu;
  h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
  return (h >> 8) * (1.0f / 16777216.0f);
}

class Timer {
 public:
  Timer() : t0_(std::chrono::steady_clock::now()) {}
  double seconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
  }
 private:
  std::chrono::steady_clock::time_point t0_;
};

}  // namespace bs
