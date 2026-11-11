#include <cstdio>

#include "app/gui.hpp"

namespace bs {
int run_gui(const std::string&) {
  std::fprintf(stderr, "The studio window is only available on Windows.\n");
  return 1;
}
}  // namespace bs
