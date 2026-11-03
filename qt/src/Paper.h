// Paper styles, drawn as GPU geometry in page units (the same spacing and
// colours as the web app's src/editor/papers.ts).
#pragma once

#include <QColor>
#include <QPointF>
#include <QString>
#include <vector>

namespace paper {

/// Triangles of one colour.
struct Layer {
  QColor color;
  std::vector<QPointF> tris;
};

/// The lines, margins and dots of `tmpl` on a w×h page (the paper colour itself not included).
std::vector<Layer> lines(const QString& tmpl, float w, float h, const QColor& paperColor);

/// Templates drawn so far (others show as blank paper for now).
bool supported(const QString& tmpl);

}  // namespace paper
