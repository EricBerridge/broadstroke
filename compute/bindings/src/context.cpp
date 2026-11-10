#include "broadstroke/compute/context.hpp"
#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/build_config.hpp"

#include <kompute/Kompute.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace broadstroke::compute {

namespace {

bool has_validation_layer() {
  for (const auto& layer : vk::enumerateInstanceLayerProperties()) {
    if (std::string_view(layer.layerName.data()) == "VK_LAYER_KHRONOS_validation") {
      return true;
    }
  }
  return false;
}

Workgroup2D choose_workgroup_2d(std::uint32_t maxInvocations,
                                std::uint32_t subgroupSize) {
  if (maxInvocations < 256) {
    return {8, 8};
  }
  if (subgroupSize != 0 && subgroupSize <= 32) {
    return {8, 8};
  }
  return {16, 16};
}

bool has_2d_variant(std::string_view baseName) {
  static const std::unordered_set<std::string_view> kernels = {
      "pencil_stroke", "watercolor_stroke", "wetness_diffuse",
      "pastel_stroke", "pastel_smear", "substrate_to_rgba",
      "apply_graphite", "apply_pigment",
  };
  return kernels.contains(baseName);
}

}  // namespace

Context::Context() : Context(ContextOptions{}) {}

Context::Context(ContextOptions options)
    : manager_(std::make_unique<kp::Manager>()) {
  const auto properties = manager_->getDeviceProperties();
  profile_.device_name = properties.deviceName.data();
  profile_.max_workgroup_invocations = properties.limits.maxComputeWorkGroupInvocations;
  profile_.validation_requested = options.validate;
  profile_.validation_layer_available = has_validation_layer();

  const auto devices = manager_->listDevices();
  if (!devices.empty()) {
    vk::PhysicalDeviceSubgroupProperties subgroupProperties{};
    vk::PhysicalDeviceProperties2 properties2{};
    properties2.pNext = &subgroupProperties;
    devices.front().getProperties2(&properties2);
    profile_.subgroup_size = subgroupProperties.subgroupSize;
  }

  profile_.workgroup_2d = choose_workgroup_2d(profile_.max_workgroup_invocations,
                                              profile_.subgroup_size);
}

Context::~Context() = default;

Buffer Context::upload(std::span<const float> data) {
  std::vector<float> copy(data.begin(), data.end());
  auto tensor = manager_->tensorT<float>(copy);
  return Buffer(std::move(tensor), this);
}

const std::vector<uint32_t>& Context::loadSpirv(const std::string& name) {
  if (auto it = shaderCache_.find(name); it != shaderCache_.end()) {
    return it->second;
  }

  const std::filesystem::path path =
      std::filesystem::path(kShaderSourceDir) / (name + ".spv");

  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    throw std::runtime_error("Cannot open SPIR-V: " + path.string());
  }
  const auto sz = static_cast<std::size_t>(f.tellg());
  if (sz == 0 || sz % 4 != 0) {
    throw std::runtime_error(
        "Malformed SPIR-V (size not a positive multiple of 4): " +
        path.string());
  }

  std::vector<uint32_t> spirv(sz / 4);
  f.seekg(0);
  f.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(sz));
  if (!f) {
    throw std::runtime_error("Short read on SPIR-V: " + path.string());
  }

  auto [it, _] = shaderCache_.emplace(name, std::move(spirv));
  return it->second;
}

void Context::dispatch(std::string_view kernelName,
                       std::initializer_list<Buffer*> buffers,
                       std::array<uint32_t, 3> groups) {
  const auto& spirv = loadSpirv(std::string(kernelName));

  std::vector<std::shared_ptr<kp::Tensor>> tensors;
  tensors.reserve(buffers.size());
  for (Buffer* b : buffers) {
    tensors.push_back(b->tensor_);
  }

  const kp::Workgroup workgroup{groups[0], groups[1], groups[2]};
  auto algo = manager_->algorithm(tensors, spirv, workgroup);

  manager_->sequence()
      ->record<kp::OpTensorSyncDevice>(tensors)
      ->record<kp::OpAlgoDispatch>(algo)
      ->record<kp::OpTensorSyncLocal>(tensors)
      ->eval();
}

std::string Context::tuned_kernel_name(std::string_view baseName) const {
  if (!has_2d_variant(baseName)) {
    return std::string(baseName);
  }

  const auto wg = profile_.workgroup_2d;
  return std::string(baseName) + "_ls" + std::to_string(wg.local_x) + "x" +
         std::to_string(wg.local_y);
}

// Buffer methods live here because they touch the complete kp::TensorT type.

std::size_t Buffer::size() const { return tensor_ ? tensor_->size() : 0; }

std::vector<float> Buffer::download() {
  return tensor_ ? tensor_->vector() : std::vector<float>{};
}

}  // namespace broadstroke::compute
