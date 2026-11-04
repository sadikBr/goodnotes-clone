// Drawing instruments (was src/editor/ruler.ts): ruler, protractor and set
// squares. One floats over the page; strokes started next to an edge lock to
// it, the protractor's curved edge guides arcs and its arms measure angles.
// Never part of the page or exports.
#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>
#include <vector>

#include "Model.h"

namespace ed {

/// Inkleaf units per millimetre (units are 1/96 inch).
constexpr double MM = 96 / 25.4;

/// A guide a stroke can lock to: a straight edge (a, dir, normal pointing
/// away from the instrument) or the protractor's arc (c, r).
struct EdgeGuide {
  bool arc = false;
  P2 a{0, 0}, dir{1, 0}, normal{0, -1};
  P2 c{0, 0};
  double r = 0;
};

/// Snaps to common angles (or to 15° steps with `coarse`), in radians.
double snapAngle(double rad, bool coarse);
/// Projects `p` onto a guide, `offset` outside it (so ink sits against the edge).
P2 projectOnto(const EdgeGuide& g, P2 p, double offset);

class Instrument {
public:
  bool visible = false;
  QString kind = "ruler";  ///< ruler, protractor, square45, square60
  double cx = 400, cy = 400;
  double angle = 0;  ///< radians, clockwise on screen
  double arms[2] = {30, 120};  ///< protractor arms, degrees (0 = right, counter-clockwise)

  Instrument();
  void setKind(const QString& kind);
  /// Shows it with its whole shape centred on (x, y).
  void show(double x, double y);
  void hide() { visible = false; }

  P2 toPage(P2 p) const;
  P2 toLocal(P2 p) const;
  /// The guide a stroke starting at `p` should lock to (within `band` outside an edge).
  std::optional<EdgeGuide> edgeNear(P2 p, double band) const;
  /// True when page point (x, y) is on the body.
  bool contains(double x, double y) const;
  P2 armEnd(int i) const;
  /// Everything the app needs to show it (zoom and view width place the ruler's grips).
  QJsonObject describe(double zoom, double viewWidth) const;

private:
  struct Line {
    P2 a, b;
  };
  struct Shape {
    QString outline, marks;
    std::vector<Line> lines;
    std::optional<double> arc;
    Rect box;
    std::vector<P2> grips;
    P2 labelAt{0, 0};
  };
  Shape m_shape;
  static Shape makeShape(const QString& kind);
  static Shape polygonShape(const std::vector<P2>& verts, const std::vector<std::pair<int, int>>& markLegs);
  static QString tickMarks(P2 a, P2 b, P2 inward, double from, bool numbers);
  P2 rot(P2 v) const;
};

}  // namespace ed
