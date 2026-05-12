#include "broadstroke/support/preset.hpp"
#include "broadstroke/build_config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace broadstroke::support {

namespace {

template <typename T>
void readOptional(const nlohmann::json& j, const char* key, T& dst) {
  if (auto it = j.find(key); it != j.end()) {
    dst = it->get<T>();
  }
}

Preset parse(const nlohmann::json& j, const std::filesystem::path& origin) {
  Preset p;
  if (!j.contains("name") || !j.contains("kind")) {
    throw std::runtime_error("preset " + origin.string() +
                             ": missing required field 'name' or 'kind'");
  }
  p.name = j.at("name").get<std::string>();
  p.kind = j.at("kind").get<std::string>();

  if (p.kind != "cold_press" && p.kind != "canvas" &&
      p.kind != "smooth_bristol") {
    throw std::runtime_error("preset " + origin.string() +
                             ": unknown kind '" + p.kind + "'");
  }

  if (auto it = j.find("base_color"); it != j.end()) {
    auto arr = it->get<std::vector<float>>();
    if (arr.size() != 3) {
      throw std::runtime_error("preset " + origin.string() +
                               ": base_color must have 3 components");
    }
    p.base_color = {arr[0], arr[1], arr[2]};
  }

  readOptional(j, "absorption_base",        p.absorption_base);
  readOptional(j, "heightfield_amplitude",  p.heightfield_amplitude);
  readOptional(j, "noise_base_frequency",   p.noise_base_frequency);
  readOptional(j, "noise_octaves",          p.noise_octaves);
  readOptional(j, "noise_persistence",      p.noise_persistence);
  readOptional(j, "noise_lacunarity",       p.noise_lacunarity);
  readOptional(j, "weave_period_pixels",    p.weave_period_pixels);
  readOptional(j, "weave_amplitude",        p.weave_amplitude);
  readOptional(j, "grain_anisotropy",       p.grain_anisotropy);
  readOptional(j, "grain_direction_degrees",p.grain_direction_degrees);

  return p;
}

}  // namespace

Preset loadPresetFromFile(const std::filesystem::path& path) {
  std::ifstream f(path);
  if (!f) {
    throw std::runtime_error("cannot open preset: " + path.string());
  }
  nlohmann::json j;
  try {
    f >> j;
  } catch (const nlohmann::json::parse_error& e) {
    throw std::runtime_error("preset " + path.string() + ": " + e.what());
  }
  return parse(j, path);
}

Preset loadPreset(const std::string& name) {
  std::filesystem::path path =
      std::filesystem::path(kResourceDir) / "supports" / (name + ".json");
  return loadPresetFromFile(path);
}

}  // namespace broadstroke::support
