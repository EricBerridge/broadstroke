#include "canvas.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace broadstroke::host {

namespace {

inline std::uint32_t group_count(std::uint32_t extent, std::uint32_t local) {
  return (extent + local - 1) / local;
}

}  // namespace

Canvas::Canvas(compute::Context& ctx, const support::SupportData& substrate,
               std::array<float, 3> base_color)
    : ctx_(ctx), w_(substrate.width), h_(substrate.height),
      base_color_(base_color) {
  heightfield_ = ctx_.upload(substrate.heightfield);
  tooth_       = ctx_.upload(substrate.tooth);
  absorption_  = ctx_.upload(substrate.absorption);
}

std::size_t Canvas::add_graphite_layer(std::array<float, 3> color) {
  std::vector<float> zero(static_cast<std::size_t>(w_) * h_, 0.0f);
  auto layer = std::make_unique<GraphiteLayer>();
  layer->density = ctx_.upload(zero);
  layer->color   = color;
  layers_.push_back({LayerEntry::Kind::Graphite, std::move(layer), nullptr});
  return layers_.size() - 1;
}

std::size_t Canvas::add_pigment_layer(std::array<float, 3> K,
                                      std::array<float, 3> S,
                                      float thickness_scale) {
  std::vector<float> zero(static_cast<std::size_t>(w_) * h_, 0.0f);
  auto layer = std::make_unique<PigmentLayer>();
  layer->pigment         = ctx_.upload(zero);
  layer->wetness         = ctx_.upload(zero);
  layer->K               = K;
  layer->S               = S;
  layer->thickness_scale = thickness_scale;
  layers_.push_back({LayerEntry::Kind::Pigment, nullptr, std::move(layer)});
  return layers_.size() - 1;
}

bool Canvas::stroke_aabb(const tools::Stroke& s, float radius, AABB& out) const {
  if (s.points.empty()) return false;
  float xmin = s.points[0].x, xmax = xmin;
  float ymin = s.points[0].y, ymax = ymin;
  for (const auto& p : s.points) {
    xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x);
    ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y);
  }
  // Pad by radius + 1 (kernel reads ±radius around the closest point).
  const float pad = radius + 1.0f;
  xmin -= pad; ymin -= pad; xmax += pad; ymax += pad;

  const int x0 = std::max(0, static_cast<int>(std::floor(xmin)));
  const int y0 = std::max(0, static_cast<int>(std::floor(ymin)));
  const int x1 = std::min(static_cast<int>(w_), static_cast<int>(std::ceil(xmax)));
  const int y1 = std::min(static_cast<int>(h_), static_cast<int>(std::ceil(ymax)));
  if (x1 <= x0 || y1 <= y0) return false;
  out.x0 = static_cast<std::uint32_t>(x0);
  out.y0 = static_cast<std::uint32_t>(y0);
  out.w  = static_cast<std::uint32_t>(x1 - x0);
  out.h  = static_cast<std::uint32_t>(y1 - y0);
  return true;
}

void Canvas::dispatch_2d(std::string_view kernel,
                         std::initializer_list<compute::Buffer*> bufs,
                         const AABB& box) {
  const auto workgroup = ctx_.workgroup_2d();
  ctx_.dispatch(ctx_.tuned_kernel_name(kernel), bufs,
                {group_count(box.w, workgroup.local_x),
                 group_count(box.h, workgroup.local_y), 1});
}

namespace {

// Slice a stroke into chunks of at most `max_segments` segments, with a
// 1-segment overlap so adjacent chunks join cleanly.
std::vector<tools::Stroke> chunk_stroke(const tools::Stroke& s,
                                        std::size_t max_segments) {
  std::vector<tools::Stroke> out;
  if (s.points.size() <= max_segments + 1) {
    out.push_back(s);
    return out;
  }
  for (std::size_t i = 0; i + 1 < s.points.size(); i += max_segments) {
    tools::Stroke chunk;
    const std::size_t end = std::min(i + max_segments + 1, s.points.size());
    chunk.points.assign(s.points.begin() + i, s.points.begin() + end);
    out.push_back(std::move(chunk));
  }
  return out;
}

}  // namespace

void Canvas::stroke_pencil(std::size_t layer_idx, const tools::Stroke& s,
                           const tools::PencilPreset& tool) {
  if (layer_idx >= layers_.size() ||
      layers_[layer_idx].kind != LayerEntry::Kind::Graphite) {
    throw std::runtime_error("stroke_pencil: layer is not graphite");
  }
  if (s.points.size() < 2) return;
  auto& layer = *layers_[layer_idx].graphite;

  for (auto& chunk : chunk_stroke(s, kSegmentsPerChunk)) {
    AABB box;
    if (!stroke_aabb(chunk, tool.tip_radius_px, box)) continue;
    auto packed = tools::packStroke(chunk);
    auto stroke_buf = ctx_.upload(packed);
    std::vector<float> params = {
        static_cast<float>(w_), static_cast<float>(h_),
        static_cast<float>(chunk.points.size()),
        tool.tip_radius_px, tool.tip_softness, tool.deposit_strength,
        static_cast<float>(box.x0), static_cast<float>(box.y0),
    };
    auto params_buf = ctx_.upload(params);
    dispatch_2d("pencil_stroke",
                {&layer.density, &tooth_, &stroke_buf, &params_buf}, box);
  }
}

void Canvas::stroke_watercolor(std::size_t layer_idx, const tools::Stroke& s,
                               const tools::BrushPreset& tool) {
  if (layer_idx >= layers_.size() ||
      layers_[layer_idx].kind != LayerEntry::Kind::Pigment) {
    throw std::runtime_error("stroke_watercolor: layer is not pigment");
  }
  if (s.points.size() < 2) return;
  auto& layer = *layers_[layer_idx].pigment;

  for (auto& chunk : chunk_stroke(s, kSegmentsPerChunk)) {
    AABB box;
    if (!stroke_aabb(chunk, tool.tip_radius_px, box)) continue;
    auto packed = tools::packStroke(chunk);
    auto stroke_buf = ctx_.upload(packed);
    std::vector<float> params = {
        static_cast<float>(w_), static_cast<float>(h_),
        static_cast<float>(chunk.points.size()),
        tool.tip_radius_px, tool.tip_softness,
        tool.water_load, tool.pigment_load,
        static_cast<float>(box.x0), static_cast<float>(box.y0),
    };
    auto params_buf = ctx_.upload(params);
    dispatch_2d("watercolor_stroke",
                {&layer.pigment, &layer.wetness, &tooth_, &stroke_buf,
                 &params_buf}, box);
  }
}

void Canvas::stroke_pastel(std::size_t layer_idx, const tools::Stroke& s,
                           const tools::PastelPreset& tool) {
  if (layer_idx >= layers_.size() ||
      layers_[layer_idx].kind != LayerEntry::Kind::Pigment) {
    throw std::runtime_error("stroke_pastel: layer is not pigment");
  }
  if (s.points.size() < 2) return;
  auto& layer = *layers_[layer_idx].pigment;

  for (auto& chunk : chunk_stroke(s, kSegmentsPerChunk)) {
    AABB box;
    if (!stroke_aabb(chunk, tool.tip_radius_px, box)) continue;
    auto packed = tools::packStroke(chunk);
    auto stroke_buf = ctx_.upload(packed);
    std::vector<float> params = {
        static_cast<float>(w_), static_cast<float>(h_),
        static_cast<float>(chunk.points.size()),
        tool.tip_radius_px, tool.tip_softness, tool.deposit_strength,
        static_cast<float>(box.x0), static_cast<float>(box.y0),
    };
    auto params_buf = ctx_.upload(params);
    dispatch_2d("pastel_stroke",
                {&layer.pigment, &tooth_, &stroke_buf, &params_buf}, box);
  }
}

void Canvas::smear_pastel(std::size_t layer_idx,
                          float ax, float ay, float bx, float by,
                          const tools::PastelPreset& tool) {
  if (layer_idx >= layers_.size() ||
      layers_[layer_idx].kind != LayerEntry::Kind::Pigment) {
    throw std::runtime_error("smear_pastel: layer is not pigment");
  }
  if (tool.smear_strength <= 0.0f) return;
  auto& layer = *layers_[layer_idx].pigment;

  // AABB around drag segment, padded by smear_radius.
  AABB box;
  tools::Stroke pseudo;
  pseudo.points = {{ax, ay, 0.0f}, {bx, by, 0.0f}};
  if (!stroke_aabb(pseudo, tool.smear_radius_px, box)) return;

  // pastel_smear is read in / write out — needs a scratch buffer for the
  // input, then we move-assign the result back to the layer.
  auto in_copy_v = layer.pigment.download();
  auto pigment_in  = ctx_.upload(in_copy_v);
  auto pigment_out = ctx_.upload(in_copy_v);  // initial contents irrelevant

  std::vector<float> params = {
      static_cast<float>(w_), static_cast<float>(h_),
      ax, ay, bx, by,
      tool.smear_radius_px, tool.smear_softness,
      static_cast<float>(tool.smear_taps), tool.smear_strength,
      static_cast<float>(box.x0), static_cast<float>(box.y0),
  };
  auto params_buf = ctx_.upload(params);
  dispatch_2d("pastel_smear",
              {&pigment_in, &pigment_out, &params_buf}, box);

  // Replace layer pigment with smeared output.
  auto result = pigment_out.download();
  layer.pigment = ctx_.upload(result);
}

void Canvas::diffuse(std::size_t layer_idx, std::uint32_t steps,
                     float water_diffusion, float pigment_diffusion,
                     float dry_rate, float dt) {
  if (layer_idx >= layers_.size() ||
      layers_[layer_idx].kind != LayerEntry::Kind::Pigment) {
    throw std::runtime_error("diffuse: layer is not pigment");
  }
  if (steps == 0) return;
  auto& layer = *layers_[layer_idx].pigment;

  // Allocate a second pair of buffers for the ping-pong. After the loop
  // the "current" buffers may be the originals or the scratch — swap them
  // back into the layer at the end so the layer always owns the live state.
  std::vector<float> zero(static_cast<std::size_t>(w_) * h_, 0.0f);
  compute::Buffer p_a = std::move(layer.pigment);
  compute::Buffer w_a = std::move(layer.wetness);
  compute::Buffer p_b = ctx_.upload(zero);
  compute::Buffer w_b = ctx_.upload(zero);

  AABB full{0, 0, w_, h_};

  for (std::uint32_t i = 0; i < steps; ++i) {
    std::vector<float> params = {
        static_cast<float>(w_), static_cast<float>(h_),
        water_diffusion, pigment_diffusion, dry_rate, dt,
        static_cast<float>(full.x0), static_cast<float>(full.y0),
    };
    auto params_buf = ctx_.upload(params);

    if ((i & 1u) == 0u) {
      dispatch_2d("wetness_diffuse",
                  {&p_a, &p_b, &w_a, &w_b, &absorption_, &params_buf}, full);
    } else {
      dispatch_2d("wetness_diffuse",
                  {&p_b, &p_a, &w_b, &w_a, &absorption_, &params_buf}, full);
    }
  }

  // After (steps) ping-pongs starting with a→b at i=0, the live buffers
  // are 'b' if steps is odd, 'a' if even.
  if ((steps & 1u) == 1u) {
    layer.pigment = std::move(p_b);
    layer.wetness = std::move(w_b);
  } else {
    layer.pigment = std::move(p_a);
    layer.wetness = std::move(w_a);
  }
}

std::vector<float> Canvas::compose(std::array<float, 3> light_dir,
                                   float ambient, float height_scale) {
  // Substrate pass: writes the initial RGBA from heightfield + base color.
  std::vector<float> rgba_zero(static_cast<std::size_t>(w_) * h_ * 4, 0.0f);
  auto rgba = ctx_.upload(rgba_zero);

  AABB full{0, 0, w_, h_};
  {
    std::vector<float> params = {
        static_cast<float>(w_), static_cast<float>(h_),
        base_color_[0], base_color_[1], base_color_[2],
        light_dir[0], light_dir[1], light_dir[2],
        ambient, height_scale,
    };
    auto params_buf = ctx_.upload(params);
    dispatch_2d("substrate_to_rgba",
                {&heightfield_, &rgba, &params_buf}, full);
  }

  // Layer passes in insertion order.
  for (auto& entry : layers_) {
    if (entry.kind == LayerEntry::Kind::Graphite) {
      auto& g = *entry.graphite;
      std::vector<float> params = {
          static_cast<float>(w_), static_cast<float>(h_),
          light_dir[0], light_dir[1], light_dir[2],
          ambient, height_scale,
          g.color[0], g.color[1], g.color[2],
      };
      auto params_buf = ctx_.upload(params);
      dispatch_2d("apply_graphite",
                  {&rgba, &g.density, &heightfield_, &params_buf}, full);
    } else {
      auto& p = *entry.pigment;
      std::vector<float> params = {
          static_cast<float>(w_), static_cast<float>(h_),
          p.K[0], p.K[1], p.K[2],
          p.S[0], p.S[1], p.S[2],
          p.thickness_scale,
      };
      auto params_buf = ctx_.upload(params);
      dispatch_2d("apply_pigment",
                  {&rgba, &p.pigment, &params_buf}, full);
    }
  }

  return rgba.download();
}

}  // namespace broadstroke::host
