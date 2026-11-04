// Developer and CI check while the editor moves from the script to C++
// (docs/MIGRATION.md): inkleaf --compare-editor <folder> runs the same
// editing sessions through both editors and fails on any difference.
#pragma once

#include <QImage>
#include <QJsonValue>
#include <QString>
#include <functional>

int compareEditor(const QString& outDir);

/// One step of an editing session: what the editor returned (if anything),
/// its state (ids numbered in order of appearance) and its overlay.
using EditorStepFn = std::function<void(const QString& session, const QString& step, const QJsonValue& result, const QJsonValue& state, const QImage& overlay)>;
/// The editing sessions through the C++ editor alone (--golden).
void runEditorSessions(const EditorStepFn& onStep);
