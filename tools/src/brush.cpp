#include "broadstroke/tools/brush.hpp"
#include "broadstroke/build_config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace broadstroke::tools {

namespace {

template <typename T>
void readOptional(const nlohmann::json& j, const char* key, T& dst) {
  if (auto it = j.find(key); it != j.end()) {
    dst = it->get<T>();
  }
}

BrushPreset parse(const nlohmann::json& j, const std::filesystem::path& origin) {
  BrushPreset p;
  if (!j.contains("name") || !j.contains("kind")) {
    throw std::runtime_error("brush " + origin.string() +
                             ": missing required field 'name' or 'kind'");
  }
  p.name = j.at("name").get<std::string>();
  p.kind = j.at("kind").get<std::string>();
  if (p.kind != "brush") {
    throw std::runtime_error("brush " + origin.string() +
                             ": unknown kind '" + p.kind + "'");
  }

  readOptional(j, "tip_radius_px", p.tip_radius_px);
  readOptional(j, "tip_softness",  p.tip_softness);
  readOptional(j, "water_load",    p.water_load);
  readOptional(j, "pigment_load",  p.pigment_load);
  return p;
}

}  // namespace

BrushPreset loadBrushFromFile(const std::filesystem::path& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open brush: " + path.string());
  nlohmann::json j;
  try { f >> j; }
  catch (const nlohmann::json::parse_error& e) {
    throw std::runtime_error("brush " + path.string() + ": " + e.what());
  }
  return parse(j, path);
}

BrushPreset loadBrush(const std::string& name) {
  std::filesystem::path path =
      std::filesystem::path(kResourceDir) / "tools" / (name + ".json");
  return loadBrushFromFile(path);
}

}  // namespace broadstroke::tools
