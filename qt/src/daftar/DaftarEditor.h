// The daftar's actions in the open notebook (was the editor half of
// qt/js/daftarQt.ts): a new lesson, an outline, pasting students, exporting
// a class and adding a lesson page. Each returns what the canvas should do
// next: {reload, reloadAll, again, pageId, goTo, y, toast, error, plain}.
#pragma once

#include <QVariantMap>

#include "../editor/EditorCore.h"

namespace daftar {

/// Runs one action by name (InkCanvas::daftar); unknown names give {}.
QVariant runInEditor(ed::EditorCore& core, const QString& fn, const QVariantList& args);

}  // namespace daftar
