#pragma once

#include "broadstroke/compute/buffer.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/support/support_data.hpp"
#include "broadstroke/tools/pigment.hpp"
#include "broadstroke/tools/pencil.hpp"
#include "broadstroke/tools/brush.hpp"
#include "broadstroke/tools/pastel.hpp"
#include "broadstroke/tools/stroke.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace broadstroke::host {

// One painted layer on the canvas. Layers are rendered in insertion
// order during compose(): substrate first, then each layer's apply_*
// pass on top. Each layer holds its own GPU field(s).
struct GraphiteLayer {
  compute::Buffer density;          // W*H, [0, 1]
  std::array<float, 3> color = {0.10f, 0.09f, 0.09f};
};

struct PigmentLayer {
  compute::Buffer pigment;          // W*H concentration, [0, ~3]
  compute::Buffer wetness;          // W*H, [0, ~4]
  std::array<float, 3> K = {0.5f, 0.5f, 0.5f};
  std::array<float, 3> S = {0.5f, 0.5f, 0.5f};
  float thickness_scale = 2.0f;
};

// A canvas owns the substrate fields and an ordered list of layers.
// Strokes mutate the appropriate layer's field via dispatch; compose()
// runs substrate_to_rgba once then walks the layer list.
class Canvas {
 public:
  Canvas(compute::Context& ctx, const support::SupportData& substrate,
         std::array<float, 3> base_color);

  // Layer management. Returns the index, used by stroke() calls.
  std::size_t add_graphite_layer(std::array<float, 3> color);
  std::size_t add_pigment_layer(std::array<float, 3> K, std::array<float, 3> S,
                                float thickness_scale = 2.0f);

  // Apply a stroke to a specific layer. Each routes to its kernel and
  // uses the stroke's AABB for bounded-box dispatch when possible.
  // Long strokes are chunked transparently to stay under TDR.
  void stroke_pencil(std::size_t layer_idx, const tools::Stroke& s,
                     const tools::PencilPreset& tool);
  void stroke_watercolor(std::size_t layer_idx, const tools::Stroke& s,
                         const tools::BrushPreset& tool);
  void stroke_pastel(std::size_t layer_idx, const tools::Stroke& s,
                     const tools::PastelPreset& tool);
  void smear_pastel(std::size_t layer_idx, float ax, float ay, float bx, float by,
                    const tools::PastelPreset& tool);

  // Run N reaction-diffusion steps on a watercolor layer (between strokes
  // or at the end of a wet pass).
  void diffuse(std::size_t pigment_layer_idx, std::uint32_t steps,
               float water_diffusion = 0.20f,
               float pigment_diffusion = 0.18f,
               float dry_rate = 0.08f,
               float dt = 1.0f);

  // Compose to RGBA, return W*H*4 floats (linear). Host tonemaps.
  std::vector<float> compose(std::array<float, 3> light_dir,
                             float ambient = 0.78f,
                             float height_scale = 12.0f);

  std::uint32_t width()  const { return w_; }
  std::uint32_t height() const { return h_; }

 private:
  // Stroke chunking — long strokes split into N-segment chunks to stay
  // well under Windows TDR (~2s GPU time). 64 segments per chunk gives a
  // generous budget at our typical stroke radii / canvas sizes.
  static constexpr std::size_t kSegmentsPerChunk = 64;

  // Bounded-box helper: computes the AABB of a stroke padded by `radius`
  // and clamped to the canvas. Returns false if the stroke misses the
  // canvas entirely.
  struct AABB { std::uint32_t x0, y0, w, h; };
  bool stroke_aabb(const tools::Stroke& s, float radius, AABB& out) const;

  void dispatch_2d(std::string_view kernel,
                   std::initializer_list<compute::Buffer*> bufs,
                   const AABB& box);

  compute::Context& ctx_;
  std::uint32_t w_, h_;
  std::array<float, 3> base_color_;
  compute::Buffer heightfield_;
  compute::Buffer tooth_;
  compute::Buffer absorption_;

  // Layers are kept in heap-allocated wrappers so add_* can return stable
  // indices and we can hold mixed types in one ordered vector.
  struct LayerEntry {
    enum class Kind { Graphite, Pigment };
    Kind kind;
    std::unique_ptr<GraphiteLayer> graphite;
    std::unique_ptr<PigmentLayer>  pigment;
  };
  std::vector<LayerEntry> layers_;
};

}  // namespace broadstroke::host
