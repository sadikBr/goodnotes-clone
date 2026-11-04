// The maths toolkit for QML: working out what's written, typesetting it, and
// pictures of graphs (for the graph dialog's preview).
#pragma once

#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class MathKit : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(MathKit)
  QML_SINGLETON
public:
  explicit MathKit(QObject* parent = nullptr) : QObject(parent) {}
  /// { ok, kind, answer, pretty, var, error } (see mathx::calculate).
  Q_INVOKABLE QVariantMap calculate(const QString& text) const;
  /// The expression typeset (× ² √ π −).
  Q_INVOKABLE QString pretty(const QString& text) const;
  /// Whether each line is a function of x it can plot: [{ ok, error }].
  Q_INVOKABLE QVariantList checkFunctions(const QString& lines) const;
  /// A y window showing the functions between xmin and xmax: { ymin, ymax }
  /// (spikes near asymptotes left out); empty when nothing can be drawn.
  Q_INVOKABLE QVariantMap fitY(const QString& lines, double xmin, double xmax) const;
  /// A graph ({ functions, xmin, xmax, ymin, ymax, grid }) drawn w×h, as a PNG data: URL.
  Q_INVOKABLE QString graphPicture(const QVariantMap& spec, int w, int h) const;
};
