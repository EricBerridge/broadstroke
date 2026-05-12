// GPU-vs-CPU equivalence test for the Stage 0 sanity kernel. The same
// validation pattern (dispatch on GPU, run reference on CPU, compare) is
// what every later physics kernel will use.

#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/reference/hello_double.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr std::uint32_t kLocalSizeX = 64;
constexpr float kTolerance = 1e-6f;
}  // namespace

int main() {
  std::vector<float> input;
  input.reserve(8192);
  for (int i = 0; i < 8192; ++i) {
    input.push_back(static_cast<float>(i) * 0.5f - 200.0f);
  }

  try {
    broadstroke::compute::Context ctx;
    auto buf = ctx.upload(input);

    const std::uint32_t groups =
        (static_cast<std::uint32_t>(input.size()) + kLocalSizeX - 1) /
        kLocalSizeX;
    ctx.dispatch("hello_double", {&buf}, {groups, 1, 1});

    const auto gpu = buf.download();
    const auto cpu = broadstroke::reference::hello_double(input);

    if (gpu.size() != cpu.size()) {
      std::fprintf(stderr, "FAIL: size mismatch gpu=%zu cpu=%zu\n", gpu.size(),
                   cpu.size());
      return 1;
    }
    for (std::size_t i = 0; i < gpu.size(); ++i) {
      if (std::fabs(gpu[i] - cpu[i]) > kTolerance) {
        std::fprintf(stderr, "FAIL: index %zu gpu=%.6f cpu=%.6f\n", i,
                     static_cast<double>(gpu[i]), static_cast<double>(cpu[i]));
        return 1;
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
    return 2;
  }
  return 0;
}
