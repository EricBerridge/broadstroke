#include "broadstroke/tools/pencil.hpp"
#include "broadstroke/build_config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace broadstroke::tools {

namespace {

template <typename T>
void readOptional(const nlohmann::json& j, const char* key, T& dst) {
  if (auto it = j.find(key); it != j.end()) {
    dst = it->get<T>();
  }
}

PencilPreset parse(const nlohmann::json& j, const std::filesystem::path& origin) {
  PencilPreset p;
  if (!j.contains("name") || !j.contains("kind")) {
    throw std::runtime_error("pencil " + origin.string() +
                             ": missing required field 'name' or 'kind'");
  }
  p.name = j.at("name").get<std::string>();
  p.kind = j.at("kind").get<std::string>();
  if (p.kind != "pencil") {
    throw std::runtime_error("pencil " + origin.string() +
                             ": unknown kind '" + p.kind + "'");
  }

  if (auto it = j.find("color"); it != j.end()) {
    auto arr = it->get<std::vector<float>>();
    if (arr.size() != 3) {
      throw std::runtime_error("pencil " + origin.string() +
                               ": color must have 3 components");
    }
    p.color = {arr[0], arr[1], arr[2]};
  }

  readOptional(j, "tip_radius_px",    p.tip_radius_px);
  readOptional(j, "tip_softness",     p.tip_softness);
  readOptional(j, "deposit_strength", p.deposit_strength);

  return p;
}

}  // namespace

PencilPreset loadPencilFromFile(const std::filesystem::path& path) {
  std::ifstream f(path);
  if (!f) {
    throw std::runtime_error("cannot open pencil: " + path.string());
  }
  nlohmann::json j;
  try {
    f >> j;
  } catch (const nlohmann::json::parse_error& e) {
    throw std::runtime_error("pencil " + path.string() + ": " + e.what());
  }
  return parse(j, path);
}

PencilPreset loadPencil(const std::string& name) {
  std::filesystem::path path =
      std::filesystem::path(kResourceDir) / "tools" / (name + ".json");
  return loadPencilFromFile(path);
}

}  // namespace broadstroke::tools
