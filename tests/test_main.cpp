#include <cstring>

#include "check.hpp"

// Runs every registered test, or only those whose name contains argv[1].
int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int ran = 0;
  for (const bstest::Case& c : bstest::cases()) {
    if (filter && !std::strstr(c.name, filter)) continue;
    int before = bstest::failures();
    std::printf("%s\n", c.name);
    std::fflush(stdout);
    c.fn();
    if (bstest::failures() != before) std::printf("  ^ failed\n");
    ++ran;
  }
  std::printf("%d tests, %d failed checks\n", ran, bstest::failures());
  return bstest::failures() == 0 ? 0 : 1;
}
