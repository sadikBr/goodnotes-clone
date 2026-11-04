// The library's storage (the Rust core) as the daftar and cover setup use
// it. A failed call throws StoreError with the core's message.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <stdexcept>
#include <vector>

#include "../editor/Model.h"

namespace store {

struct StoreError : std::runtime_error {
  explicit StoreError(const QString& m) : std::runtime_error(m.toStdString()) {}
  QString message() const { return QString::fromStdString(what()); }
};

QJsonObject createNotebook(const QJsonObject& input);
/// The library: { notebooks, folders, … } (as the "library" command gives it).
QJsonObject library();
QJsonObject notebook(const QString& id);
QJsonArray pages(const QString& notebookId);
QJsonObject addPage(const QString& notebookId, int index, const QString& tmpl);
void setPageTemplate(const QString& pageId, const QString& tmpl, const QString& paperColor);
/// An empty section clears it.
void setPageSection(const QString& pageId, const QString& section);
void setPageThumbnail(const QString& pageId, const QString& dataUrl);
std::vector<ed::El> elements(const QString& pageId);
void saveElements(const QString& pageId, const std::vector<ed::El>& els);
void setNotebookMeta(const QString& id, const QString& meta);
void setNotebookCover(const QString& id, const QString& color);

}  // namespace store
