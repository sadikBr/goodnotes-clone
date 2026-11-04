#include "DocKinds.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "../Core.h"
#include "../daftar/Store.h"

namespace {
struct Refresh {
  ~Refresh() { emit InkleafCore::instance()->libraryChanged(); }
};
}  // namespace

QVariantMap DocKinds::whiteboard(const QString& title, const QString& folderId) {
  const Refresh refresh;
  try {
    const QJsonObject nb = store::createNotebook({{"title", title.trimmed().isEmpty() ? QStringLiteral("Whiteboard") : title.trimmed()}, {"folderId", folderId.isEmpty() ? QJsonValue() : QJsonValue(folderId)},
                                                  {"coverColor", "#475569"}, {"template", "dotted"}, {"paperColor", "#ffffff"}, {"width", 2400}, {"height", 1600}, {"coverPage", false}});
    const QString id = nb["id"].toString();
    store::setNotebookMeta(id, QStringLiteral("{\"kind\":\"whiteboard\"}"));
    return store::notebook(id).toVariantMap();
  } catch (const store::StoreError& e) {
    emit failed(e.message());
    return {};
  }
}

QString DocKinds::kindOf(const QString& notebookId) const {
  if (notebookId.isEmpty()) return {};
  try {
    const QJsonValue meta = store::notebook(notebookId)["meta"];
    const QJsonObject o = meta.isString() ? QJsonDocument::fromJson(meta.toString().toUtf8()).object() : meta.toObject();
    return o["kind"].toString();
  } catch (const store::StoreError&) {
    return {};
  }
}
