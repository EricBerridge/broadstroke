#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace kp {
template <class T> class TensorT;
}  // namespace kp

namespace broadstroke::compute {

class Context;

// Opaque handle to a GPU-resident float buffer. Construction and destruction
// of the underlying Kompute tensor go through Context; client code never
// touches Vulkan types.
class Buffer {
 public:
  // Empty / unbound. Useful for struct fields that are filled in later
  // (e.g. SupportField channels assembled one upload at a time).
  Buffer() = default;

  std::size_t size() const;
  bool empty() const { return !tensor_; }

  // Sync GPU contents back to host and return a copy.
  std::vector<float> download();

 private:
  friend class Context;

  Buffer(std::shared_ptr<kp::TensorT<float>> tensor, Context* ctx) noexcept
      : tensor_(std::move(tensor)), ctx_(ctx) {}

  std::shared_ptr<kp::TensorT<float>> tensor_;
  Context* ctx_ = nullptr;
};

}  // namespace broadstroke::compute
