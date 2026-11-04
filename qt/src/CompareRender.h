// Developer and CI check while drawing moves from the scripts to C++
// (docs/MIGRATION.md): inkleaf --compare-render <folder> draws each case
// both ways and fails if any picture differs; differing ones are saved there.
#pragma once

#include <QString>

int compareRender(const QString& outDir);
