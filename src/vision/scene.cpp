#include "vision/scene.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/json.hpp"
#include "stb_image.h"

namespace bs {
namespace {

constexpr long long kMaxFileBytes = 4LL * 1000 * 1000;      // a scene file is a few kilobytes
constexpr long long kMaxLayerPixels = 120LL * 1000 * 1000;  // as for photos

std::filesystem::path fs_path(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string directory_of(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// A whole number in [lo, hi] from a JSON member; false if it is anything else.
bool whole(const Json* j, int lo, int hi, int& out) {
  if (!j || !j->is_number()) return false;
  double v = j->number;
  if (!(v >= lo && v <= hi) || v != std::floor(v)) return false;
  out = static_cast<int>(v);
  return true;
}

}  // namespace

const SceneItem* Scene::find(int id) const {
  for (const SceneItem& it : items)
    if (it.id == id) return &it;
  return nullptr;
}

bool Scene::fits(int photo_w, int photo_h) const {
  if (item.empty() || photo_w <= 0 || photo_h <= 0) return false;
  // Compare the two shapes by cross-multiplying; 2% covers the rounding of
  // a layer saved at a different size.
  double a = static_cast<double>(item.w) * photo_h, b = static_cast<double>(item.h) * photo_w;
  return std::fabs(a - b) <= 0.02 * std::max(a, b);
}

std::string scene_path_for(const std::string& photo_path) {
  size_t slash = photo_path.find_last_of("/\\");
  size_t dot = photo_path.find_last_of('.');
  std::string stem =
      (dot == std::string::npos || (slash != std::string::npos && dot < slash)) ? photo_path : photo_path.substr(0, dot);
  return stem + ".scene.json";
}

bool file_exists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(fs_path(path), ec);
}

Scene load_scene(const std::string& path, std::string* error) {
  auto fail = [&](const std::string& msg) {
    if (error) *error = "scene file '" + path + "': " + msg;
    return Scene{};
  };

  std::string text;
  {
    std::error_code ec;
    auto size = std::filesystem::file_size(fs_path(path), ec);
    if (ec) return fail("cannot be read");
    if (static_cast<long long>(size) > kMaxFileBytes) return fail("is too large");
    std::ifstream in(fs_path(path), std::ios::binary);
    if (!in) return fail("cannot be read");
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  Json doc;
  std::string why;
  if (!parse_json(text, doc, &why)) return fail("is not valid JSON (" + why + ")");
  if (!doc.is_object()) return fail("must be a JSON object");

  int version = 0;
  if (!whole(doc.get("version"), 1, 1, version)) return fail("\"version\" must be 1");

  Scene scene;
  const Json* items = doc.get("items");
  if (!items || !items->is_array()) return fail("\"items\" must be a list");
  if (items->array.empty() || items->array.size() > 255) return fail("must have 1 to 255 items");
  for (const Json& j : items->array) {
    if (!j.is_object()) return fail("each item must be an object");
    SceneItem it;
    if (!whole(j.get("id"), 1, 255, it.id)) return fail("an item's \"id\" must be a whole number from 1 to 255");
    std::string tag = "item " + std::to_string(it.id);
    if (scene.find(it.id)) return fail(tag + " appears twice");
    const Json* name = j.get("name");
    if (!name || !name->is_string() || name->string.empty()) return fail(tag + " needs a \"name\"");
    it.name = name->string;
    if (j.get("parent") && !whole(j.get("parent"), 0, 255, it.parent))
      return fail(tag + ": \"parent\" must be an item id, or 0");
    if (!whole(j.get("importance"), 0, 3, it.importance))
      return fail(tag + ": \"importance\" must be 0, 1, 2 or 3");
    if (const Json* t = j.get("treatment")) {
      if (t->type != Json::Type::String) return fail(tag + ": \"treatment\" must be a string");
      it.treatment = t->string;
    }
    scene.items.push_back(std::move(it));
  }
  for (const SceneItem& it : scene.items) {
    // Following parents must end at 0: no missing parent, no cycle.
    int steps = 0;
    for (int p = it.parent; p != 0; p = scene.find(p)->parent) {
      if (!scene.find(p)) return fail("item " + std::to_string(it.id) + " has a parent that is not an item");
      if (++steps > 255) return fail("item " + std::to_string(it.id) + " is its own ancestor");
    }
  }
  if (doc.get("main_subject")) {
    if (!whole(doc.get("main_subject"), 0, 255, scene.main_subject) ||
        (scene.main_subject != 0 && !scene.find(scene.main_subject)))
      return fail("\"main_subject\" must be an item id, or 0");
  }

  const Json* layer = doc.get("item_layer");
  if (!layer || !layer->is_string() || layer->string.empty()) return fail("\"item_layer\" must name a PNG file");
  // The layer lives beside the scene file; a path elsewhere is refused.
  if (layer->string.find_first_of("/\\:") != std::string::npos)
    return fail("\"item_layer\" must be a file name without a folder");
  std::string layer_path = directory_of(path) + layer->string;

  int w = 0, h = 0, comp = 0;
  if (!stbi_info(layer_path.c_str(), &w, &h, &comp)) return fail("cannot read the item layer '" + layer->string + "'");
  if (w <= 0 || h <= 0 || static_cast<long long>(w) * h > kMaxLayerPixels)
    return fail("the item layer has unsupported dimensions");
  // Ids are stored as gray levels, so only a plain 8-bit grayscale image
  // keeps them: a palette or a 16-bit image would be converted on load.
  if (comp != 1 || stbi_is_16_bit(layer_path.c_str()))
    return fail("the item layer must be an 8-bit grayscale PNG");
  unsigned char* data = stbi_load(layer_path.c_str(), &w, &h, &comp, 1);
  if (!data) return fail("cannot decode the item layer '" + layer->string + "'");
  scene.item = Plane<uint8_t>(w, h);
  std::copy(data, data + static_cast<size_t>(w) * h, scene.item.v.begin());
  stbi_image_free(data);

  bool known[256] = {};
  known[0] = true;
  for (const SceneItem& it : scene.items) known[it.id] = true;
  for (uint8_t v : scene.item.v)
    if (!known[v]) return fail("the item layer has pixels of value " + std::to_string(v) + ", which no item has");
  return scene;
}

}  // namespace bs
