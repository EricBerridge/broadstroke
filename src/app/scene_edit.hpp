// Saving a scene file that was corrected in the studio window (spec 2.10).
// The engine only reads scene files (spec 2.13); this is the application's.
#pragma once

#include <string>
#include <vector>

#include "vision/scene.hpp"

namespace bs {

// Where the scene file is kept as the observe step wrote it, once it has
// been corrected: "<stem>.scene.observed.json" beside "<stem>.scene.json".
std::string observed_copy_path(const std::string& scene_path);

// Writes the importance, treatment and edges of each of `items` into the
// scene file at `scene_path` (UTF-8), matched by id. Everything else in the
// file stays as it is, in its order. The first time a file is corrected, the
// file as it was is copied to observed_copy_path() and the file is marked
// ("edited": true); *kept_observed says whether that copy was made now. The
// file is replaced only when the new text is complete on disk. Returns false
// and sets *error on failure; the file is then unchanged.
bool save_scene_items(const std::string& scene_path, const std::vector<SceneItem>& items, bool* kept_observed = nullptr,
                      std::string* error = nullptr);

}  // namespace bs
