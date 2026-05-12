#include "broadstroke/reference/hello_double.hpp"

namespace broadstroke::reference {

std::vector<float> hello_double(std::span<const float> data) {
  std::vector<float> out(data.begin(), data.end());
  for (auto& v : out) {
    v *= 2.0f;
  }
  return out;
}

}  // namespace broadstroke::reference
