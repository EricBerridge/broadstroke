// Scripted studies: small fixed scenes that exercise one piece of the physics
// at a time, so each model can be inspected without the planner in the way.
#pragma once

#include <string>
#include <vector>

namespace bs {

std::vector<std::string> study_names();
// Renders the named study to `out_path`. Returns false (and prints why) on failure.
bool run_study(const std::string& name, const std::string& out_path);

}  // namespace bs
