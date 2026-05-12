#pragma once

#include <span>
#include <vector>

namespace broadstroke::reference {

// CPU mirror of compute/kernels/hello_double.comp.
std::vector<float> hello_double(std::span<const float> data);

}  // namespace broadstroke::reference
