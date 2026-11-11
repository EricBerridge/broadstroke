// Minimal test harness: TEST(name) { ... CHECK(cond); } with no dependencies.
#pragma once

#include <cmath>
#include <cstdio>
#include <vector>

namespace bstest {

struct Case {
  const char* name;
  void (*fn)();
};
inline std::vector<Case>& cases() {
  static std::vector<Case> c;
  return c;
}
inline int& failures() {
  static int f = 0;
  return f;
}
struct Registrar {
  Registrar(const char* name, void (*fn)()) { cases().push_back({name, fn}); }
};

}  // namespace bstest

#define TEST(name)                                                   \
  static void bstest_##name();                                       \
  static bstest::Registrar bstest_reg_##name(#name, &bstest_##name); \
  static void bstest_##name()

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
      ++bstest::failures();                                              \
    }                                                                    \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                                        \
  do {                                                                                               \
    double bstest_a = (a), bstest_b = (b);                                                           \
    if (!(std::fabs(bstest_a - bstest_b) <= (tol))) {                                                \
      std::printf("    FAILED %s:%d: %s = %g, expected %g +- %g\n", __FILE__, __LINE__, #a, bstest_a, \
                  bstest_b, static_cast<double>(tol));                                               \
      ++bstest::failures();                                                                          \
    }                                                                                                \
  } while (0)
