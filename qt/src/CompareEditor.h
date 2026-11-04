// Developer and CI check while the editor moves from the script to C++
// (docs/MIGRATION.md): inkleaf --compare-editor <folder> runs the same
// editing sessions through both editors and fails on any difference.
#pragma once

#include <QString>

int compareEditor(const QString& outDir);
