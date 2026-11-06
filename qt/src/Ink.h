// Ink: how a stroke's points become the shape drawn on screen.
//
// The same rules as the web app (src/editor/ink.ts): points are eased
// ("streamline" smoothing), each pen style turns pressure into width the
// way perfect-freehand does, and the fountain and brush pens follow drawing
// speed. The result is a triangle mesh for the GPU, built once per stroke:
// zooming and scrolling never re-draw committed ink.
#pragma once

#include <QColor>
#include <QPointF>
#include <QString>
#include <vector>

namespace ink {

struct Point {
  float x, y, p;  // page units, pressure 0..1
};

enum class Pen { Fountain, Ballpoint, Pencil, Calligraphy, Brush, Highlighter, Marker, Watercolor, Neon, Construction, Tape };

Pen penFromName(const QString& style, bool highlighter);
QString penName(Pen pen);
/// Fountain and brush widths follow speed.
bool speedSensitive(Pen pen);
/// Saved strokes of this style are drawn as one mesh (else as a picture:
/// pencil grain, calligraphy, and the effect pens).
bool meshDrawn(const QString& style, bool highlighter);
/// Masking tape's band for a pen width (what a tape stroke saves as its width).
float tapeWidth(float penWidth);
/// The colour a live stroke is drawn in (see-through pens are lighter).
QColor liveColor(const QColor& c, Pen pen);
/// How wide a pen's line comes out at medium pressure, for a chosen width (what the phone pad previews).
float lineWidth(Pen pen, float width);

/// The smoothing levels (Settings → Writing).
constexpr float kSmoothOff = 0.f, kSmoothLight = 0.2f, kSmoothNormal = 0.38f, kSmoothStrong = 0.62f;

/// Streamline smoothing: each point eases toward the pen from the previous one.
/// `into` may hold an earlier result for a growing stroke; it is extended.
void smoothPoints(const std::vector<Point>& points, std::vector<Point>& into, float streamline);

/// Turns drawing speed into a pressure-like value (fountain and brush pens).
class SpeedInk {
public:
  /// `raw` is the stylus pressure, or < 0 for mouse and touch.
  float next(float x, float y, double tMs, float raw);

private:
  float m_level = 0.55f;
  bool m_has = false;
  float m_x = 0, m_y = 0;
  double m_t = 0;
};

/// A drawable stroke: triangles in page units.
struct Mesh {
  std::vector<QPointF> tris;  // 3 per triangle
  std::vector<QPointF> glow;  // neon: a soft halo under the line
};

/// Builds the mesh of a stroke as written. `width` is the stroke's nominal width.
/// `dashed`: construction lines (0 solid, 1 dashes, 2 dots; `gap` scales the spacing).
void strokeMesh(const std::vector<Point>& raw, Pen pen, float width, float streamline, Mesh& out, int dashed = 0, float gap = 1.f);

/// Distance from point to segment (hit-testing for the eraser).
float segmentDistance(QPointF p, QPointF a, QPointF b);

}  // namespace ink
