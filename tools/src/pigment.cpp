#include "broadstroke/tools/pigment.hpp"
#include "broadstroke/build_config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace broadstroke::tools {

namespace {

std::array<float, 3> readTriplet(const nlohmann::json& j, const char* key,
                                 const std::filesystem::path& origin,
                                 std::array<float, 3> fallback) {
  auto it = j.find(key);
  if (it == j.end()) return fallback;
  auto arr = it->get<std::vector<float>>();
  if (arr.size() != 3) {
    throw std::runtime_error("pigment " + origin.string() + ": " + key +
                             " must have 3 components");
  }
  return {arr[0], arr[1], arr[2]};
}

PigmentPreset parse(const nlohmann::json& j, const std::filesystem::path& origin) {
  PigmentPreset p;
  if (!j.contains("name") || !j.contains("kind")) {
    throw std::runtime_error("pigment " + origin.string() +
                             ": missing required field 'name' or 'kind'");
  }
  p.name = j.at("name").get<std::string>();
  p.kind = j.at("kind").get<std::string>();
  if (p.kind != "pigment") {
    throw std::runtime_error("pigment " + origin.string() +
                             ": unknown kind '" + p.kind + "'");
  }
  p.K      = readTriplet(j, "K",      origin, p.K);
  p.S      = readTriplet(j, "S",      origin, p.S);
  p.swatch = readTriplet(j, "swatch", origin, p.swatch);
  return p;
}

}  // namespace

PigmentPreset loadPigmentFromFile(const std::filesystem::path& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open pigment: " + path.string());
  nlohmann::json j;
  try { f >> j; }
  catch (const nlohmann::json::parse_error& e) {
    throw std::runtime_error("pigment " + path.string() + ": " + e.what());
  }
  return parse(j, path);
}

PigmentPreset loadPigment(const std::string& name) {
  std::filesystem::path path =
      std::filesystem::path(kResourceDir) / "media" / (name + ".json");
  return loadPigmentFromFile(path);
}

}  // namespace broadstroke::tools
