#include "broadstroke/tools/pastel.hpp"
#include "broadstroke/build_config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace broadstroke::tools {

namespace {

template <typename T>
void readOptional(const nlohmann::json& j, const char* key, T& dst) {
  if (auto it = j.find(key); it != j.end()) dst = it->get<T>();
}

std::array<float, 3> readTriplet(const nlohmann::json& j, const char* key,
                                 const std::filesystem::path& origin,
                                 std::array<float, 3> fallback) {
  auto it = j.find(key);
  if (it == j.end()) return fallback;
  auto arr = it->get<std::vector<float>>();
  if (arr.size() != 3) {
    throw std::runtime_error("pastel " + origin.string() + ": " + key +
                             " must have 3 components");
  }
  return {arr[0], arr[1], arr[2]};
}

PastelPreset parse(const nlohmann::json& j, const std::filesystem::path& origin) {
  PastelPreset p;
  if (!j.contains("name") || !j.contains("kind")) {
    throw std::runtime_error("pastel " + origin.string() +
                             ": missing required field 'name' or 'kind'");
  }
  p.name = j.at("name").get<std::string>();
  p.kind = j.at("kind").get<std::string>();
  if (p.kind != "pastel") {
    throw std::runtime_error("pastel " + origin.string() +
                             ": unknown kind '" + p.kind + "'");
  }

  readOptional(j, "tip_radius_px",    p.tip_radius_px);
  readOptional(j, "tip_softness",     p.tip_softness);
  readOptional(j, "deposit_strength", p.deposit_strength);
  p.K = readTriplet(j, "K", origin, p.K);
  p.S = readTriplet(j, "S", origin, p.S);

  readOptional(j, "smear_radius_px", p.smear_radius_px);
  readOptional(j, "smear_softness",  p.smear_softness);
  readOptional(j, "smear_taps",      p.smear_taps);
  readOptional(j, "smear_strength",  p.smear_strength);

  return p;
}

}  // namespace

PastelPreset loadPastelFromFile(const std::filesystem::path& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open pastel: " + path.string());
  nlohmann::json j;
  try { f >> j; }
  catch (const nlohmann::json::parse_error& e) {
    throw std::runtime_error("pastel " + path.string() + ": " + e.what());
  }
  return parse(j, path);
}

PastelPreset loadPastel(const std::string& name) {
  std::filesystem::path path =
      std::filesystem::path(kResourceDir) / "tools" / (name + ".json");
  return loadPastelFromFile(path);
}

}  // namespace broadstroke::tools
