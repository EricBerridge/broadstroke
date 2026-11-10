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

struct ContextOptions {
  bool validate = false;
};

struct Workgroup2D {
  std::uint32_t local_x = 16;
  std::uint32_t local_y = 16;
};

struct DeviceProfile {
  std::string device_name;
  std::uint32_t subgroup_size = 0;
  std::uint32_t max_workgroup_invocations = 0;
  Workgroup2D workgroup_2d{};
  bool validation_requested = false;
  bool validation_layer_available = false;
};

// Owns the Kompute Manager (one Vulkan device + queues) and is the only
// entry point used by host/analyzer/planner code. Stage 0 goal: nothing
// outside compute/bindings ever includes <kompute/Kompute.hpp>.
class Context {
 public:
  Context();
  explicit Context(ContextOptions options);
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

  [[nodiscard]] const DeviceProfile& profile() const noexcept { return profile_; }
  [[nodiscard]] Workgroup2D workgroup_2d() const noexcept { return profile_.workgroup_2d; }
  [[nodiscard]] std::string tuned_kernel_name(std::string_view baseName) const;

 private:
  const std::vector<uint32_t>& loadSpirv(const std::string& name);

  std::unique_ptr<kp::Manager> manager_;
  std::unordered_map<std::string, std::vector<uint32_t>> shaderCache_;
  DeviceProfile profile_;
};

}  // namespace broadstroke::compute
