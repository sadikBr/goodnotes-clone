// Pen strokes (was src/editor/ink.ts and the stroke drawing in render.ts):
// smooth filled outlines (perfect-freehand), pencil grain, the calligraphy
// nib and the highlighter.
#pragma once

#include <QJsonObject>
#include <QString>
#include <array>
#include <vector>

class JsCanvas;

namespace draw {

using Pt = std::array<double, 3>;  ///< x, y, pressure

/// Streamline smoothing levels.
constexpr double kStreamlineNormal = 0.38;

/// The stroke's outline polygons (split at sharp corners).
std::vector<std::vector<std::pair<double, double>>> strokeOutline(const std::vector<Pt>& points, const QString& style, double width, bool last = true, double streamline = kStreamlineNormal);
/// Drops highlighter points that barely move from the previous one.
std::vector<Pt> steadyHighlight(const std::vector<Pt>& pts, double width);
/// The curve strokePath draws (quadratics through midpoints), as a polyline.
std::vector<std::pair<double, double>> flattenSmooth(const std::vector<Pt>& pts);

/// A stroke element (as the editor holds it: tool, style, color, width, points, smooth).
/// `live`: still being drawn (its end isn't tapered yet).
void drawStroke(JsCanvas& c, const QJsonObject& el, bool live = false);

}  // namespace draw
