// Element geometry (was src/editor/geometry.ts): bounds, hit tests, the
// lasso, edit handles, resizing, precise erasing and selection transforms.
#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "Model.h"

namespace ed {

double distToSegment(double px, double py, double ax, double ay, double bx, double by);
bool pointInPolygon(double x, double y, const std::vector<P2>& poly);
Rect rectOfPts(const std::vector<P2>& pts, double pad = 0);
/// Text box height including word-wrapped lines (width < 0: unlimited).
double textHeight(const QString& text, double fontSize, double width, const El& style);
/// The unrotated box of a text box, sticky note or picture.
Rect boxOf(const El& el);
Rect bounds(const El& el);
std::optional<Rect> unionRects(const std::vector<Rect>& rects);
bool rectContains(const Rect& r, double x, double y);
/// A shape's separate strokes (a path with breaks is several polylines).
std::vector<std::vector<P2>> shapeParts(const Shape& s);
bool isClosedShape(const Shape& s);
/// A line with two ends and no inside: a line, an arrow, a double arrow.
bool isSegmentKind(const QString& kind);
/// Whether a point (with radius) touches an element (the eraser).
bool hitTest(const El& el, double x, double y, double radius);
/// Tap-to-select: closed shapes also count when tapped inside.
bool hitForSelect(const El& el, double x, double y, double radius);

/// A lasso prepared for testing many elements.
class Lasso {
public:
  explicit Lasso(const std::vector<P2>& points);
  /// 60% of a line's points inside (up to ~96 looked at), else its centre.
  bool holds(const El& el) const;

private:
  std::vector<double> m_xs, m_ys;
  bool inside(double x, double y) const;
};

El translate(const El& el, double dx, double dy);
/// Whether the pointer sat (nearly) still for the last `ms` of the stroke.
bool isHeld(const std::vector<double>& times, const std::vector<Pt>& pts, double ms, double tolerance);
/// The chosen shape from a drag between a and b (Shift: squares, circles,
/// 45° lines); none for "auto" or a drag too small to see.
std::optional<Shape> buildShape(const QString& kind, P2 a, P2 b, bool constrain = false);

bool isResizable(const El& el);
/// Geometric box (without stroke width).
Rect geomBox(const El& el);
struct Handle {
  QString id;  ///< nw n ne e se s sw w, or p0 p1 (line ends)
  double x, y;
};
std::vector<Handle> handlesFor(const El& el);
QString handleAt(const El& el, double x, double y, double radius);
QString handleCursor(const QString& handle);
El resizeElement(const El& el, const QString& handle, P2 p, bool keepAspect = false);
/// A shape as another kind in the same box (line ↔ arrow keep their ends).
El convertShape(const El& el, const QString& kind);
/// Rotates by `angle` and scales around `pivot`, then moves by (dx, dy).
Shape transformShape(const Shape& s, P2 pivot, double angle, double scale, double dx = 0, double dy = 0);
/// The part of an element under the eraser removed: none when untouched,
/// else its remaining pieces (empty when nothing is left).
std::optional<std::vector<El>> eraseElement(const El& el, double x, double y, double radius);

/// x' = a·x + c·y + e, y' = b·x + d·y + f.
struct Affine {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  bool identity() const { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
};
P2 applyAffine(const Affine& m, P2 p);
Affine scaleAbout(double ax, double ay, double sx, double sy);
Affine rotateAbout(double cx, double cy, double angle);
/// Handles around a selection box; the rotation handle ("rot") sits below it.
std::vector<Handle> groupHandles(const Rect& box, double zoom);
QString groupHandleAt(const Rect& box, double x, double y, double zoom);
struct GroupTransform {
  Affine m;
  double sx = 1, sy = 1, angle = 0;
};
GroupTransform groupTransform(const Rect& box, const QString& handle, P2 start, P2 p, bool constrain);
El transformElement(const El& el, const Affine& m, double sx, double sy, double angle);

// --- drafting: the help a held line or shape gets ------------------------------

/// A straight piece of a drawing: a line, an arrow, a side of a polygon.
struct DraftLine {
  P2 a, b;
  QString id;  ///< the element it belongs to
};
/// The straight lines on a page (lines, arrows, polygon sides); not `skip`.
std::vector<DraftLine> draftingLines(const std::vector<ElPtr>& els, const QString& skip = {});
/// Where lines may join: line ends, polygon corners, points; not `skip`.
std::vector<P2> draftingCorners(const std::vector<ElPtr>& els, const QString& skip = {});
/// The nearest corner within `radius`, if any.
std::optional<P2> nearestCorner(const std::vector<P2>& corners, P2 p, double radius);
/// The line that passes within `radius` of `p` (the nearest), if any.
std::optional<DraftLine> lineThrough(const std::vector<DraftLine>& lines, P2 p, double radius);
/// How a held line's direction settled: on a round angle (every 15°), parallel
/// to a line, or square to the line it starts on; none when it didn't.
struct AngleSnap {
  double angle;  ///< the direction (radians, screen coordinates)
  QString why;   ///< "", "round", "parallel", "square"
  std::optional<DraftLine> ref;
};
AngleSnap snapDirection(double angle, const std::vector<DraftLine>& lines, const std::optional<DraftLine>& startsOn, double toleranceDeg = 1.5);
/// The smaller angle between two lines, in degrees (0–90 folded to 0–180 as drawn).
double angleBetween(const DraftLine& s, P2 from, P2 to);
/// A box or ellipse that's nearly square or round becomes exactly so (in place).
bool squareUp(Shape& s, double tolerance = 0.06);
/// Its size in page units, for the size label: width × height (sides of a box,
/// axes of an ellipse; else as drawn).
std::pair<double, double> shapeSize(const Shape& s);

}  // namespace ed
