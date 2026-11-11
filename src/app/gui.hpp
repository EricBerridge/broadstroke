// The studio window: pick a photo, a medium and a surface, and watch the
// artwork being made stroke by stroke.
#pragma once

#include <string>

namespace bs {

// Opens the window and runs until it is closed. Returns a process exit code.
// On platforms without a GUI implementation it prints a message and returns 1.
int run_gui(const std::string& initial_photo);

}  // namespace bs
