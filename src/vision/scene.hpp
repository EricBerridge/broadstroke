// What the photograph shows (spec 2.13): the items in the scene and which
// item each pixel belongs to. A scene is input, like the photo. It is read
// from a scene file written by a separate observe step or by hand; nothing
// in the engine works it out.
#pragma once

#include "core/image.hpp"

namespace bs {

// How an item's outline is to be stated: crisp, gradual, or merged into
// its surroundings.
enum class Edges : uint8_t { Hard, Soft, Lost };

struct SceneItem {
  int id = 0;          // the value of this item's pixels in the item layer, 1..255
  std::string name;
  int parent = 0;      // id of the item this one is a part of; 0 for none
  int importance = 1;  // 0 background, 1 supporting, 2 matters, 3 the main subject and its key parts
  std::string treatment;  // how the item is to be worked ("skin", "hair", ...); empty if not stated
  Edges edges = Edges::Hard;  // hard if not stated
};

struct Scene {
  std::vector<SceneItem> items;
  int main_subject = 0;  // id of the most important item; 0 if not stated
  // The item layer: for each pixel, the id of the deepest item (a part
  // before its whole) that covers it; 0 where no item does. It may have any
  // resolution with the photo's proportions.
  Plane<uint8_t> item;
  // The orientation layer, if the scene has one: for each pixel the unit
  // normal of the surface seen there, as r to the right, g downward and b
  // toward the viewer. It has the item layer's proportions; empty if the
  // scene does not state orientation.
  Image normal;

  bool empty() const { return item.empty(); }
  const SceneItem* find(int id) const;
  // Whether the item layer has the proportions of a photo of this size.
  bool fits(int photo_w, int photo_h) const;
};

// Reads a scene file (JSON, with the item layer as an 8-bit grayscale PNG
// beside it). Paths are UTF-8. The file is untrusted input and is checked
// throughout. Returns an empty scene and sets *error on failure.
Scene load_scene(const std::string& path, std::string* error = nullptr);

// Where a photo's scene file is looked for: the photo's path with its
// extension replaced by ".scene.json".
std::string scene_path_for(const std::string& photo_path);

// Whether a file exists at this UTF-8 path.
bool file_exists(const std::string& path);

}  // namespace bs
