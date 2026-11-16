#include "app/scene_edit.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/json.hpp"

namespace bs {
namespace {

std::filesystem::path fs_path(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

bool write_file(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.flush();
  return static_cast<bool>(out);
}

const char* edges_word(Edges e) {
  return e == Edges::Soft ? "soft" : (e == Edges::Lost ? "lost" : "hard");
}

}  // namespace

std::string observed_copy_path(const std::string& scene_path) {
  const std::string tail = ".json";
  if (scene_path.size() > tail.size() && scene_path.compare(scene_path.size() - tail.size(), tail.size(), tail) == 0)
    return scene_path.substr(0, scene_path.size() - tail.size()) + ".observed.json";
  return scene_path + ".observed";
}

bool save_scene_items(const std::string& scene_path, const std::vector<SceneItem>& items, bool* kept_observed,
                      std::string* error) {
  auto fail = [&](const std::string& msg) {
    if (error) *error = "scene file '" + scene_path + "': " + msg;
    return false;
  };
  if (kept_observed) *kept_observed = false;

  std::string text;
  {
    std::ifstream in(fs_path(scene_path), std::ios::binary);
    if (!in) return fail("cannot be read");
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  Json doc;
  std::string why;
  if (!parse_json(text, doc, &why)) return fail("is not valid JSON (" + why + ")");
  if (!doc.is_object()) return fail("must be a JSON object");

  bool found_list = false;
  for (auto& top : doc.object) {
    if (top.first != "items" || !top.second.is_array()) continue;
    found_list = true;
    for (Json& j : top.second.array) {
      const Json* id = j.get("id");
      if (!j.is_object() || !id || !id->is_number()) continue;
      for (const SceneItem& it : items) {
        if (static_cast<double>(it.id) != id->number) continue;
        j.member("importance") = Json::of(static_cast<double>(it.importance));
        // A member the file does not state is added only if it now says
        // something other than what leaving it out means.
        if (j.get("treatment") || !it.treatment.empty()) j.member("treatment") = Json::of(it.treatment);
        if (j.get("edges") || it.edges != Edges::Hard) j.member("edges") = Json::of(std::string(edges_word(it.edges)));
        // Whether it is brought out: stated, or not stated at all. Likewise
        // whether it is erased, which a file states only if it is.
        auto leave_out = [&j](const char* name) {
          for (auto member = j.object.begin(); member != j.object.end(); ++member)
            if (member->first == name) {
              j.object.erase(member);
              break;
            }
        };
        if (it.bring_out >= 0) j.member("bring_out") = Json::of(it.bring_out != 0);
        else leave_out("bring_out");
        if (it.erase) j.member("erase") = Json::of(true);
        else leave_out("erase");
        break;
      }
    }
  }
  if (!found_list) return fail("\"items\" must be a list");

  const Json* edited = doc.get("edited");
  const bool first_edit = !(edited && edited->type == Json::Type::Bool && edited->boolean);
  std::error_code ec;
  if (first_edit) {
    // The file as the observe step wrote it is kept, once.
    if (!write_file(fs_path(observed_copy_path(scene_path)), text)) return fail("cannot keep a copy of it as observed");
    doc.member("edited") = Json::of(true);
  }

  const std::filesystem::path target = fs_path(scene_path);
  std::filesystem::path partial = target;
  partial += ".partial";
  if (!write_file(partial, write_json(doc))) {
    std::filesystem::remove(partial, ec);
    return fail("cannot be written");
  }
  std::filesystem::rename(partial, target, ec);
  if (ec) {
    std::filesystem::remove(partial, ec);
    return fail("cannot be replaced");
  }
  if (kept_observed) *kept_observed = first_edit;
  return true;
}

}  // namespace bs
