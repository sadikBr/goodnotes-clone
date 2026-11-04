// Lists and small helpers QML needs (were in the scripts): the paper
// templates, list detection for the text editor and instrument angle snapping.
#pragma once

#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>
#include <vector>

struct PaperTemplate {
  QString id, label, category;
  bool pagePreview, hidden;
};
/// Every paper, in the order the paper picker shows them.
const std::vector<PaperTemplate>& paperTemplates();

class Catalog : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(Catalog)
  QML_SINGLETON
public:
  explicit Catalog(QObject* parent = nullptr) : QObject(parent) {}
  /// [{id, label, category, preview ("page" or ""), hidden}].
  Q_INVOKABLE QVariantList templates() const;
  /// "bullet", "number" or "" for the paragraph the text ends in.
  Q_INVOKABLE QString currentList(const QString& text) const;
  /// An instrument angle snapped to whole (or, coarse, 15°) degrees.
  Q_INVOKABLE double snapAngle(double rad, bool coarse) const;
};
