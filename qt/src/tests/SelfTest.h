// A small test runner built into the app (inkleaf --self-test), for the C++
// code that replaced the TypeScript and its Vitest tests (docs/MIGRATION.md).
#pragma once

#include <QString>
#include <cmath>
#include <functional>
#include <vector>

namespace selftest {

struct Test {
  const char* group;
  const char* name;
  std::function<void()> run;
};
std::vector<Test>& registry();
struct Register {
  Register(const char* group, const char* name, std::function<void()> f) { registry().push_back({group, name, std::move(f)}); }
};
/// Records a failed check (file:line and what was checked).
void fail(const char* file, int line, const QString& what);
int runAll();

inline bool close(double a, double b, int digits = 2) { return std::abs(a - b) < 0.5 * std::pow(10.0, -digits); }

}  // namespace selftest

#define ST_CAT2(a, b) a##b
#define ST_CAT(a, b) ST_CAT2(a, b)
#define TEST(group, name) \
  static void ST_CAT(test_, __LINE__)(); \
  static selftest::Register ST_CAT(reg_, __LINE__)(group, name, ST_CAT(test_, __LINE__)); \
  static void ST_CAT(test_, __LINE__)()
#define CHECK(cond) \
  do { \
    if (!(cond)) selftest::fail(__FILE__, __LINE__, #cond); \
  } while (0)
#define CHECK_EQ(a, b) \
  do { \
    const auto va_ = (a); \
    const auto vb_ = (b); \
    if (!(va_ == vb_)) selftest::fail(__FILE__, __LINE__, QStringLiteral(#a " == " #b)); \
  } while (0)
#define CHECK_CLOSE(a, b, digits) \
  do { \
    const double va_ = (a), vb_ = (b); \
    if (!selftest::close(va_, vb_, digits)) selftest::fail(__FILE__, __LINE__, QStringLiteral(#a " ≈ " #b " (%1 vs %2)").arg(va_).arg(vb_)); \
  } while (0)
