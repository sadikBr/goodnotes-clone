#include "Strokes.h"

#include <QHash>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>

#include "../Canvas.h"
#include "Freehand.h"

namespace draw {
namespace {

using P2 = std::pair<double, double>;

double jsRound(double v) { return std::floor(v + 0.5); }

std::vector<Pt> pointsOf(const QJsonObject& el) {
  std::vector<Pt> out;
  for (const QJsonValue& v : el["points"].toArray()) {
    const QJsonArray a = v.toArray();
    out.push_back({a[0].toDouble(), a[1].toDouble(), a.size() > 2 ? a[2].toDouble() : 0.5});
  }
  return out;
}

/// perfect-freehand settings per pen style (size is the stroke's nominal width).
FreehandOptions options(const QString& style, double width, bool last) {
  FreehandOptions o;
  o.smoothing = 0.62;
  o.streamline = 0.38;
  o.simulatePressure = false;
  o.last = last;
  if (style == "ballpoint") {
    o.size = std::max(0.8, width * 0.8);
    o.thinning = 0;
  } else if (style == "pencil") {
    o.size = std::max(0.8, width * 0.78);
    o.thinning = 0.15;
  } else if (style == "brush") {
    o.size = width * 1.25;
    o.thinning = 0.82;
    o.startTaper = width * 3;
    o.endTaper = width * 4;
  } else {
    // Fountain: at medium pressure the width matches the size chosen.
    o.size = width * 0.875;
    o.thinning = 0.5;
  }
  return o;
}

/// Indices where the stroke turns sharply (more than ~80° within a short distance).
std::vector<int> sharpCorners(const std::vector<Pt>& points, double width) {
  const double reach = std::max(3.0, width * 1.2);
  const int n = int(points.size());
  std::vector<double> turn(size_t(n), 0);
  for (int i = 1; i < n - 1; i++) {
    int a = i - 1;
    while (a > 0 && std::hypot(points[i][0] - points[a][0], points[i][1] - points[a][1]) < reach) a--;
    int b = i + 1;
    while (b < n - 1 && std::hypot(points[b][0] - points[i][0], points[b][1] - points[i][1]) < reach) b++;
    const double ux = points[i][0] - points[a][0], uy = points[i][1] - points[a][1];
    const double vx = points[b][0] - points[i][0], vy = points[b][1] - points[i][1];
    const double lu = std::hypot(ux, uy), lv = std::hypot(vx, vy);
    if (lu < reach * 0.5 || lv < reach * 0.5) continue;
    turn[size_t(i)] = std::acos(std::max(-1.0, std::min(1.0, (ux * vx + uy * vy) / (lu * lv))));
  }
  std::vector<int> corners;
  const double threshold = (80 * M_PI) / 180;
  for (int i = 2; i < n - 2; i++) {
    // The sharpest point of each turn (a local maximum).
    if (turn[size_t(i)] > threshold && turn[size_t(i)] >= turn[size_t(i - 1)] && turn[size_t(i)] > turn[size_t(i + 1)]) {
      if (corners.empty() || i - corners.back() > 2) corners.push_back(i);
    }
  }
  return corners;
}

/// The stroke's points after streamline smoothing (each point eases toward the pen).
std::vector<Pt> smoothPoints(const std::vector<Pt>& points, double streamline) {
  const double t = 0.15 + (1 - streamline) * 0.85;
  std::vector<Pt> into;
  for (size_t i = 0; i < points.size(); i++) {
    const Pt& r = points[i];
    if (i == 0) {
      into.push_back(r);
      continue;
    }
    const Pt& p = into[i - 1];
    into.push_back({p[0] + (r[0] - p[0]) * t, p[1] + (r[1] - p[1]) * t, r[2]});
  }
  return into;
}

std::vector<FPoint> fpoints(const std::vector<Pt>& pts, size_t from, size_t to) {
  std::vector<FPoint> out;
  for (size_t i = from; i < to; ++i) out.push_back({pts[i][0], pts[i][1], pts[i][2]});
  return out;
}

/// Adds a closed, smooth path through an outline polygon (quadratics through midpoints).
void outlinePath(Canvas& c, const std::vector<P2>& outline) {
  const size_t n = outline.size();
  if (n < 3) return;
  auto mid = [](P2 a, P2 b) { return P2{(a.first + b.first) / 2, (a.second + b.second) / 2}; };
  const P2 m0 = mid(outline[n - 1], outline[0]);
  c.moveTo(m0.first, m0.second);
  for (size_t i = 0; i < n; i++) {
    const P2 p = outline[i];
    const P2 m = mid(p, outline[(i + 1) % n]);
    c.quadraticCurveTo(p.first, p.second, m.first, m.second);
  }
  c.closePath();
}

// Pencil grain: a small tile of per-pixel alpha noise, tinted per colour.
QImage grainTile(const QString& color) {
  thread_local QHash<QString, QImage> cache;
  if (auto it = cache.constFind(color); it != cache.constEnd()) return *it;
  Canvas t(48, 48);
  t.setFill(color);
  t.fillRect(0, 0, 48, 48);
  QByteArray px = t.pixels(0, 0, 48, 48);
  long long seed = 7;
  for (int i = 3; i < px.size(); i += 4) {
    seed = (seed * 16807) % 2147483647;
    px[i] = char(70 + (seed % 186));  // alpha 70..255
  }
  t.putPixels(px, 0, 0, 48, 48);
  const QImage img = t.image();
  cache.insert(color, img);
  return img;
}

/// Calligraphy nib angle (from the horizontal, rising to the right).
const double NIB_ANGLE = -M_PI / 4;

/// A moving average over `radius` points of an evenly spaced path.
std::vector<Pt> smoothAlong(const std::vector<Pt>& pts, int radius) {
  const int n = int(pts.size());
  if (n < 3 || radius < 1) return pts;
  std::vector<Pt> out;
  for (int i = 0; i < n; i++) {
    const int r = std::min({radius, i, n - 1 - i});
    double x = 0, y = 0;
    for (int k = i - r; k <= i + r; k++) {
      x += pts[size_t(k)][0];
      y += pts[size_t(k)][1];
    }
    out.push_back({x / (2 * r + 1), y / (2 * r + 1), pts[size_t(i)][2]});
  }
  return out;
}

/// Evenly spaced, smoothed points along a calligraphy stroke.
std::vector<Pt> calligraphyPath(const std::vector<Pt>& pts, double step, int smoothing) {
  if (pts.size() < 2) return pts;
  std::vector<Pt> even{pts[0]};
  double carry = 0;
  for (size_t i = 1; i < pts.size(); i++) {
    const Pt& a = pts[i - 1];
    const Pt& b = pts[i];
    const double len = std::hypot(b[0] - a[0], b[1] - a[1]);
    double t = step - carry;
    while (t <= len) {
      const double k = t / len;
      even.push_back({a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k});
      t += step;
    }
    carry = len - (t - step);
  }
  const Pt& last = pts.back();
  if (std::hypot(last[0] - even.back()[0], last[1] - even.back()[1]) > step * 0.3) even.push_back(last);
  return smoothAlong(smoothAlong(even, smoothing), smoothing);
}

/// Calligraphy: a flat nib held at a fixed angle, swept along the path.
void drawCalligraphy(Canvas& c, const QJsonObject& el, const std::vector<Pt>& points, double width, const QString& color) {
  const double ux = std::cos(NIB_ANGLE), uy = std::sin(NIB_ANGLE);
  auto half = [&](const Pt& p) { return (width * 1.4 * (0.55 + 0.6 * std::min(1.0, std::max(0.05, p[2])))) / 2; };
  // Even spacing, smoothed over about a nib width (at least ~1 mm).
  const double step = std::max(0.6, width * 0.25);
  const std::vector<Pt> pts = calligraphyPath(points, step, int(jsRound(std::max(4.0, width * 0.9) / step)));
  Q_UNUSED(el);
  c.setFill(color);
  c.setStroke(color);
  c.setLineCap("butt");
  c.setLineJoin("round");
  c.setLineWidth(std::max(0.6, width * 0.16));
  c.beginPath();
  if (pts.size() == 1) {
    const double h = half(pts[0]);
    c.moveTo(pts[0][0] - ux * h, pts[0][1] - uy * h);
    c.lineTo(pts[0][0] + ux * h, pts[0][1] + uy * h);
    c.stroke();
    return;
  }
  for (size_t i = 1; i < pts.size(); i++) {
    const Pt& a = pts[i - 1];
    const Pt& b = pts[i];
    const double ha = half(a), hb = half(b);
    std::array<P2, 4> quad = {P2{a[0] + ux * ha, a[1] + uy * ha}, P2{b[0] + ux * hb, b[1] + uy * hb}, P2{b[0] - ux * hb, b[1] - uy * hb}, P2{a[0] - ux * ha, a[1] - uy * ha}};
    // Same winding for every piece, so the non-zero fill unites them.
    double area = 0;
    for (int k = 0; k < 4; k++) area += quad[k].first * quad[(k + 1) % 4].second - quad[(k + 1) % 4].first * quad[k].second;
    if (area < 0) std::reverse(quad.begin(), quad.end());
    c.moveTo(quad[0].first, quad[0].second);
    for (int k = 1; k < 4; k++) c.lineTo(quad[k].first, quad[k].second);
    c.closePath();
  }
  c.fill("nonzero");
  // The nib's own thickness: strokes drawn along the nib stay a crisp hairline.
  c.beginPath();
  c.moveTo(pts[0][0], pts[0][1]);
  for (size_t i = 1; i < pts.size(); ++i) c.lineTo(pts[i][0], pts[i][1]);
  c.stroke();
}

/// Flat (chisel) highlighter ends: a short straight piece of marker behind each end.
void flatEndFillers(Canvas& c, const std::vector<P2>& pts, double width) {
  if (pts.size() < 2) return;
  double total = 0;
  for (size_t i = 1; i < pts.size(); i++) total += std::hypot(pts[i].first - pts[i - 1].first, pts[i].second - pts[i - 1].second);
  const double len = std::min(width / 2, total);
  auto piece = [&](P2 end, P2 towards) {
    const double dx = towards.first - end.first, dy = towards.second - end.second;
    const double d = std::hypot(dx, dy);
    if (d < 1e-6) return;
    c.moveTo(end.first, end.second);
    c.lineTo(end.first + (dx / d) * len, end.second + (dy / d) * len);
  };
  piece(pts[0], pts[1]);
  piece(pts.back(), pts[pts.size() - 2]);
}

struct Dot {
  double x, y, r;
};
/// A tap (all points within a fraction of the line width): a round dot as wide as the line.
bool tapDot(const std::vector<Pt>& pts, const QString& style, double width, Dot* out) {
  double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0, y1 = -x0;
  for (const Pt& p : pts) {
    x0 = std::min(x0, p[0]);
    y0 = std::min(y0, p[1]);
    x1 = std::max(x1, p[0]);
    y1 = std::max(y1, p[1]);
  }
  if (std::max(x1 - x0, y1 - y0) > width * 0.6) return false;
  const double k = style == "brush" ? 0.6 : style == "ballpoint" || style == "pencil" ? 0.42 : 0.5;
  *out = {(x0 + x1) / 2, (y0 + y1) / 2, std::max(0.6, width * k)};
  return true;
}

}  // namespace

std::vector<std::vector<P2>> strokeOutline(const std::vector<Pt>& raw, const QString& style, double width, bool last, double streamline) {
  FreehandOptions opts = options(style, width, last);
  // Smoothing is applied here, over the whole stroke, rather than inside perfect-freehand.
  std::vector<Pt> points = smoothPoints(raw, streamline);
  if (last && raw.size() > 1) points.back() = raw.back();
  opts.streamline = 0;
  const std::vector<int> corners = points.size() > 6 ? sharpCorners(points, width) : std::vector<int>{};
  if (corners.empty()) return {getStroke(fpoints(points, 0, points.size()), opts)};
  std::vector<std::vector<P2>> out;
  std::vector<int> cuts{0};
  cuts.insert(cuts.end(), corners.begin(), corners.end());
  cuts.push_back(int(points.size()) - 1);
  for (size_t k = 0; k + 1 < cuts.size(); k++) {
    FreehandOptions partOpts = opts;
    // Tapers belong to the stroke's real start and end only; parts meet with round caps.
    if (k != 0) {
      partOpts.startCap = true;
      partOpts.startTaper = 0;
    }
    if (k != cuts.size() - 2) {
      partOpts.endCap = true;
      partOpts.endTaper = 0;
    }
    partOpts.last = k == cuts.size() - 2 ? last : true;
    out.push_back(getStroke(fpoints(points, size_t(cuts[k]), size_t(cuts[k + 1]) + 1), partOpts));
  }
  return out;
}

std::vector<Pt> steadyHighlight(const std::vector<Pt>& pts, double width) {
  if (pts.size() < 3) return pts;
  const double min = std::max(1.5, width * 0.35);
  std::vector<Pt> out{pts[0]};
  for (size_t i = 1; i < pts.size(); i++) {
    const Pt& last = out.back();
    if (std::hypot(pts[i][0] - last[0], pts[i][1] - last[1]) >= min) out.push_back(pts[i]);
  }
  // Keep a lone dab visible.
  if (out.size() == 1) out.push_back(pts.back());
  return out;
}

std::vector<P2> flattenSmooth(const std::vector<Pt>& pts) {
  std::vector<P2> out;
  if (pts.size() < 3) {
    for (const Pt& p : pts) out.push_back({p[0], p[1]});
    return out;
  }
  out.push_back({pts[0][0], pts[0][1]});
  P2 cur = out[0];
  for (size_t i = 1; i < pts.size() - 1; i++) {
    const Pt& c = pts[i];
    const P2 end{(pts[i][0] + pts[i + 1][0]) / 2, (pts[i][1] + pts[i + 1][1]) / 2};
    const double len = std::hypot(c[0] - cur.first, c[1] - cur.second) + std::hypot(end.first - c[0], end.second - c[1]);
    const int n = std::max(1, std::min(16, int(std::ceil(len / 2))));
    for (int k = 1; k <= n; k++) {
      const double t = double(k) / n, u = 1 - t;
      out.push_back({u * u * cur.first + 2 * u * t * c[0] + t * t * end.first, u * u * cur.second + 2 * u * t * c[1] + t * t * end.second});
    }
    cur = end;
  }
  out.push_back({pts.back()[0], pts.back()[1]});
  return out;
}

void drawStroke(Canvas& c, const QJsonObject& el, bool live) {
  const std::vector<Pt> pts = pointsOf(el);
  if (pts.empty()) return;
  const QString color = el["color"].toString();
  const double width = el["width"].toDouble();
  c.save();
  c.setLineCap("round");
  c.setLineJoin("round");
  c.setStroke(color);
  if (el["tool"].toString() == "highlighter") {
    c.setGlobalAlpha(0.35);
    c.setComposite("multiply");
    c.setLineCap("butt");
    c.setLineWidth(width);
    // The smoothed curve as many short straight pieces with round joins.
    const std::vector<P2> flat = flattenSmooth(steadyHighlight(pts, width));
    c.beginPath();
    c.moveTo(flat[0].first, flat[0].second);
    if (flat.size() == 1) c.lineTo(flat[0].first + 0.01, flat[0].second);
    for (size_t i = 1; i < flat.size(); ++i) c.lineTo(flat[i].first, flat[i].second);
    flatEndFillers(c, flat, width);
    c.stroke();  // one stroke: overlapping parts don't darken
    c.restore();
    return;
  }
  const QString style = el["style"].toString().isEmpty() ? QStringLiteral("fountain") : el["style"].toString();
  Dot dot{};
  const bool isDot = tapDot(pts, style, width, &dot);
  if (isDot && style == "calligraphy") {
    c.setFill(color);
    c.beginPath();
    c.arc(dot.x, dot.y, dot.r, 0, M_PI * 2);
    c.fill();
    c.restore();
    return;
  }
  if (style == "calligraphy") {
    drawCalligraphy(c, el, pts, width, color);
    c.restore();
    return;
  }
  // Every other pen is one smooth filled outline (no beads or steps).
  c.beginPath();
  if (isDot) c.arc(dot.x, dot.y, dot.r, 0, M_PI * 2);
  else {
    const double streamline = el["smooth"].isDouble() ? el["smooth"].toDouble() : kStreamlineNormal;
    for (const auto& outline : strokeOutline(pts, style, width, !live, streamline)) outlinePath(c, outline);
  }
  if (style == "pencil") {
    double avg = 0;
    for (const Pt& p : pts) avg += p[2];
    avg /= double(pts.size());
    c.setFill(QBrush(grainTile(color)));
    c.setGlobalAlpha(0.55 + 0.4 * std::min(1.0, avg * 1.4));
  } else {
    c.setFill(color);
  }
  c.fill("nonzero");
  c.restore();
}

}  // namespace draw
