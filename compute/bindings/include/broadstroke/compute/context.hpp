#pragma once

#include "broadstroke/compute/buffer.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kp {
class Manager;
}  // namespace kp

namespace broadstroke::compute {

// Owns the Kompute Manager (one Vulkan device + queues) and is the only
// entry point used by host/analyzer/planner code. Stage 0 goal: nothing
// outside compute/bindings ever includes <kompute/Kompute.hpp>.
class Context {
 public:
  Context();
  ~Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  // Allocate a device buffer and seed it with host data.
  Buffer upload(std::span<const float> data);

  // Dispatch a kernel by short name. The .spv is loaded from
  // build_config.hpp's kShaderSourceDir on first use and cached. `groups`
  // is the number of workgroups in (x, y, z); the kernel itself declares
  // local_size_*.
  void dispatch(std::string_view kernelName,
                std::initializer_list<Buffer*> buffers,
                std::array<uint32_t, 3> groups);

 private:
  const std::vector<uint32_t>& loadSpirv(const std::string& name);

  std::unique_ptr<kp::Manager> manager_;
  std::unordered_map<std::string, std::vector<uint32_t>> shaderCache_;
};

}  // namespace broadstroke::compute
