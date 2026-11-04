#include "SelfTest.h"

#include <QtDebug>

namespace selftest {

namespace {
int g_failures = 0;
const Test* g_current = nullptr;
}  // namespace

std::vector<Test>& registry() {
  static std::vector<Test> r;
  return r;
}

void fail(const char* file, int line, const QString& what) {
  ++g_failures;
  qWarning().noquote() << QString("FAIL %1 / %2  (%3:%4)  %5").arg(g_current ? g_current->group : "?", g_current ? g_current->name : "?", QString(file).section('/', -1)).arg(line).arg(what);
}

int runAll() {
  int failedTests = 0;
  for (const Test& t : registry()) {
    g_current = &t;
    const int before = g_failures;
    t.run();
    if (g_failures != before) ++failedTests;
  }
  g_current = nullptr;
  qInfo().noquote() << QString("self-test: %1 of %2 tests failed").arg(failedTests).arg(registry().size());
  return failedTests ? 1 : 0;
}

}  // namespace selftest
