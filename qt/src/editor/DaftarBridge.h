// What the daftar script (qt/js/daftarQt.ts) reaches of the C++ editor
// until the daftar setup moves out of the scripts (docs/MIGRATION.md, step 7):
// the pages, their elements as JSON, the active page and one undo step.
#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>

#include "EditorCore.h"

class DaftarBridge : public QObject {
  Q_OBJECT
public:
  explicit DaftarBridge(ed::EditorCore* core, QObject* parent = nullptr) : QObject(parent), m_core(core) {}
  Q_INVOKABLE QString pagesJson() const {
    QJsonArray a;
    for (const auto& p : m_core->pages) a.append(p.meta);
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
  }
  Q_INVOKABLE int active() const { return m_core->active; }
  Q_INVOKABLE void activate(int i) { m_core->activate(i); }
  Q_INVOKABLE void finishTextEdit() { m_core->finishTextEdit(); }
  Q_INVOKABLE QString elementsJson(int i) const {
    QJsonArray a;
    for (const ed::ElPtr& e : m_core->load(i)) a.append(e->toJson());
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
  }
  Q_INVOKABLE void setElementsJson(const QString& json) { m_core->setElementsJson(QJsonDocument::fromJson(json.toUtf8()).array()); }
  Q_INVOKABLE void snapshot() { m_core->snapshot(); }
  Q_INVOKABLE void changed() { m_core->changed(); }

private:
  ed::EditorCore* m_core;
};
