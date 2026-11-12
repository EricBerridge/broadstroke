// What the artist sees in the subject: its colors, where its edges run, and
// which parts deserve the most care. This is observation, not rendering: the
// photograph is never copied to the canvas, only looked at.
#pragma once

#include "core/image.hpp"
#include "optics/spectrum.hpp"
#include "vision/scene.hpp"

namespace bs {

// Direction field following the structure of an image.
struct FlowField {
  Plane<Vec2> dir;         // unit vector along edges / contours (sign is arbitrary)
  Plane<float> coherence;  // 0 (no preferred direction) .. 1 (strongly oriented)
  Plane<float> strength;   // 0..1 contrast of the structure there (weak in flat areas)
};

// Structure-tensor analysis of an image in a perceptual color space.
// sigma_tensor (px) sets the scale over which orientation is pooled.
FlowField compute_flow(const Image& img, float sigma_tensor);

struct Subject {
  Image color;           // the photograph at canvas resolution, linear RGB
  Plane<float> light;    // perceptual lightness, 0..1
  Plane<float> edge;     // local edge strength, 0..1 (percentile-normalized)
  Plane<float> detail;   // importance for fine work, 0..1
  Plane<uint8_t> item;   // with a scene: the id of the item each cell belongs to (0 = none); empty without
  std::vector<std::string> treatment;  // with a scene: each item's treatment by id (256 entries, "" if none)
};

// Resamples the photograph to the canvas grid and derives the maps above.
// With a scene (spec 2.13), the detail map also follows how much each item
// matters: more care for the main subject, less for the background. Without
// one, or with an empty one, the result is exactly as before.
Subject observe(const Image& photo, int w, int h, const Scene* scene = nullptr);

// Where the outlines between items run, from a map of item ids.
struct Outlines {
  Plane<float> dist;   // px from the cell to the nearest outline (large if the map has none)
  Plane<Vec2> along;   // unit vector along that outline (sign is arbitrary)
};
Outlines find_outlines(const Plane<uint8_t>& item);

// Gaussian blur that does not carry color across an outline: each cell gets
// the weighted mean of the cells of its own item only. This is how a form is
// seen broadly without its neighbor's color mixed in.
Image blur_within_items(const Image& img, const Plane<uint8_t>& item, float sigma);

// Mean color difference (Oklab distance) between two images, evaluated on a
// grid of blocks; block = averaging size in px. Used by the artist to step
// back and compare the work with the subject.
Plane<float> block_difference(const Image& a, const Image& b, int block);
Image block_mean(const Image& a, int block);

}  // namespace bs
