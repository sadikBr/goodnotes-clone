// Kinds of documents beyond a notebook: a whiteboard (one page that grows
// as it fills up); a notebook's kind is kept in its meta.
#pragma once

#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class DocKinds : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(DocKinds)
  QML_SINGLETON
public:
  explicit DocKinds(QObject* parent = nullptr) : QObject(parent) {}

  /// A whiteboard: one big dotted page that grows as it fills up.
  Q_INVOKABLE QVariantMap whiteboard(const QString& title, const QString& folderId);
  /// The kind of document a notebook was made as ("whiteboard"; "" for a notebook).
  Q_INVOKABLE QString kindOf(const QString& notebookId) const;

signals:
  void failed(const QString& message);
};
