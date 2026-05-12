// Sanity tests for analyzer passes against a synthetic image with known
// structure: a horizontal luminance gradient overlaid with a tilted hard
// edge. We can check exact luminance values, edge magnitudes at expected
// pixels, and that flow points along the edge.

#include "broadstroke/analyzer/analyze.hpp"
#include "broadstroke/analyzer/edges.hpp"
#include "broadstroke/analyzer/flow.hpp"
#include "broadstroke/analyzer/image.hpp"
#include "broadstroke/analyzer/luminance.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace ba = broadstroke::analyzer;

namespace {

constexpr std::uint32_t kW = 64;
constexpr std::uint32_t kH = 64;

ba::Image makeTestImage() {
  ba::Image img;
  img.width = kW; img.height = kH;
  img.rgba.assign(static_cast<std::size_t>(kW) * kH * 4, 0.0f);
  // Horizontal luminance ramp from 0 (left) to 1 (right).
  for (std::uint32_t y = 0; y < kH; ++y) {
    for (std::uint32_t x = 0; x < kW; ++x) {
      const float v = static_cast<float>(x) / static_cast<float>(kW - 1);
      const std::size_t i = static_cast<std::size_t>(y) * kW + x;
      img.rgba[i * 4 + 0] = v;
      img.rgba[i * 4 + 1] = v;
      img.rgba[i * 4 + 2] = v;
      img.rgba[i * 4 + 3] = 1.0f;
    }
  }
  return img;
}

}  // namespace

int main() {
  try {
    auto img = makeTestImage();

    // Luminance: should match the ramp exactly (R=G=B → Y = R).
    auto lum = ba::luminance(img);
    for (std::uint32_t y = 0; y < kH; ++y) {
      for (std::uint32_t x = 0; x < kW; ++x) {
        const float expected = static_cast<float>(x) / (kW - 1);
        const float got = lum[static_cast<std::size_t>(y) * kW + x];
        if (std::fabs(got - expected) > 1e-5f) {
          std::fprintf(stderr, "FAIL luminance @(%u,%u) got=%.6f want=%.6f\n",
                       x, y, (double)got, (double)expected);
          return 1;
        }
      }
    }

    // Value bands: leftmost column must be band 0, rightmost band num_bands-1.
    const std::uint8_t N = 4;
    auto bands = ba::valueBands(lum, N);
    if (bands[0] != 0) {
      std::fprintf(stderr, "FAIL: leftmost pixel band %u != 0\n", bands[0]);
      return 1;
    }
    if (bands[kW - 1] != N - 1) {
      std::fprintf(stderr, "FAIL: rightmost pixel band %u != %u\n",
                   bands[kW - 1], N - 1);
      return 1;
    }

    // Sobel edges: a horizontal ramp gives a near-constant gx (~step/width)
    // away from the edges, and zero gy.
    auto grad = ba::sobelGradient(lum, kW, kH);
    // Pick an interior pixel (16, 32).
    {
      const std::size_t i = static_cast<std::size_t>(32) * kW + 16;
      const float gx_expected = 1.0f / (kW - 1);   // step per pixel along x
      if (std::fabs(grad.gx[i] - gx_expected) > 1e-4f) {
        std::fprintf(stderr, "FAIL sobel gx interior got=%.6f want=%.6f\n",
                     (double)grad.gx[i], (double)gx_expected);
        return 1;
      }
      if (std::fabs(grad.gy[i]) > 1e-4f) {
        std::fprintf(stderr, "FAIL sobel gy interior got=%.6f want=0\n",
                     (double)grad.gy[i]);
        return 1;
      }
    }

    // Flow field: gradient is +x, so the flow (along texture) should be
    // approximately ±y (perpendicular to gradient). Allow either sign of
    // dy; require |dx| << |dy|.
    auto flow = ba::structureTensorFlow(grad, kW, kH, /*smooth_radius=*/3);
    {
      const std::size_t i = static_cast<std::size_t>(32) * kW + 16;
      if (std::fabs(flow.dx[i]) > 0.1f || std::fabs(flow.dy[i]) < 0.9f) {
        std::fprintf(stderr, "FAIL flow interior got=(%.4f,%.4f); "
                     "expected ~(0, ±1)\n",
                     (double)flow.dx[i], (double)flow.dy[i]);
        return 1;
      }
    }

    // analyze() end-to-end: every per-pixel field must be populated.
    auto an = ba::analyze(img, {/*num_bands=*/N, /*flow_smooth_radius=*/3});
    if (an.width != kW || an.height != kH) {
      std::fprintf(stderr, "FAIL analyze size\n");
      return 1;
    }
    if (an.luminance.size() != static_cast<std::size_t>(kW) * kH ||
        an.edges.size()     != static_cast<std::size_t>(kW) * kH ||
        an.flow.dx.size()   != static_cast<std::size_t>(kW) * kH ||
        an.flow.dy.size()   != static_cast<std::size_t>(kW) * kH ||
        an.value_band.size()!= static_cast<std::size_t>(kW) * kH ||
        an.rgb.size()       != static_cast<std::size_t>(kW) * kH * 3) {
      std::fprintf(stderr, "FAIL analyze field sizes\n");
      return 1;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
