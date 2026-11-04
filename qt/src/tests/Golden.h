// Reference results kept from the migration (docs/MIGRATION.md): inkleaf
// --golden record|check <folder> [<diffs folder>].
#pragma once

#include <QString>

int golden(const QString& mode, const QString& dir, const QString& diffDir);
