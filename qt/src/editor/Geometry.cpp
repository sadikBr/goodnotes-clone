#include "Geometry.h"

#include <cmath>
#include <limits>

#include "../draw/Elements.h"

namespace ed {
namespace {

constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double MIN_SIZE = 4;

double jsRound(double v) { return std::floor(v + 0.5); }
double jsSign(double v) { return v > 0 ? 1 : v < 0 ? -1 : v; }

double distToPolyline(double px, double py, const std::vector<P2>& pts, bool closed) {
  if (pts.size() == 1) return std::hypot(px - pts[0][0], py - pts[0][1]);
  double best = INF;
  const size_t n = closed ? pts.size() : pts.size() - 1;
  for (size_t i = 0; i < n && !pts.empty(); i++) {
    const P2& a = pts[i];
    const P2& b = pts[(i + 1) % pts.size()];
    best = std::min(best, distToSegment(px, py, a[0], a[1], b[0], b[1]));
  }
  return best;
}

template <typename T>
Rect rectOf(const std::vector<T>& pts, double pad) {
  double x0 = INF, y0 = INF, x1 = -INF, y1 = -INF;
  for (const T& p : pts) {
    x0 = std::min(x0, p[0]);
    y0 = std::min(y0, p[1]);
    x1 = std::max(x1, p[0]);
    y1 = std::max(y1, p[1]);
  }
  if (!std::isfinite(x0)) return {0, 0, 0, 0};
  return {x0 - pad, y0 - pad, x1 - x0 + 2 * pad, y1 - y0 + 2 * pad};
}

Rect rotatedBounds(const Rect& r, double angle) {
  if (!angle) return r;
  const double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  const double c = std::abs(std::cos(angle)), s = std::abs(std::sin(angle));
  const double w = r.w * c + r.h * s, h = r.w * s + r.h * c;
  return {cx - w / 2, cy - h / 2, w, h};
}

bool isSegment(const El& el) { return el.kind == Kind::Shape && (el.shape.kind == "line" || el.shape.kind == "arrow"); }

/// Splits a polyline where a circle (cx, cy, reach) touches it; none when untouched.
template <typename T, typename Lerp>
std::optional<std::vector<std::vector<T>>> cutPolyline(const std::vector<T>& pts, double cx, double cy, double reach, Lerp lerp) {
  if (pts.empty()) return std::nullopt;
  const double step = std::max(0.5, reach / 4);
  std::vector<T> dense{pts[0]};
  for (size_t i = 1; i < pts.size(); i++) {
    const T& a = pts[i - 1];
    const T& b = pts[i];
    if (distToSegment(cx, cy, a[0], a[1], b[0], b[1]) <= reach + step) {
      const int n = int(std::ceil(std::hypot(b[0] - a[0], b[1] - a[1]) / step));
      for (int k = 1; k < n; k++) dense.push_back(lerp(a, b, double(k) / n));
    }
    dense.push_back(b);
  }
  std::vector<bool> inside(dense.size());
  bool any = false;
  for (size_t i = 0; i < dense.size(); ++i) {
    inside[i] = std::hypot(dense[i][0] - cx, dense[i][1] - cy) <= reach;
    any = any || inside[i];
  }
  if (!any) return std::nullopt;
  std::vector<std::vector<T>> runs;
  std::vector<T> cur;
  for (size_t i = 0; i < dense.size(); ++i) {
    if (inside[i]) {
      if (cur.size() >= 2) runs.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(dense[i]);
    }
  }
  if (cur.size() >= 2) runs.push_back(cur);
  return runs;
}

P2 lerp2(const P2& a, const P2& b, double t) { return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t}; }
Pt lerp3(const Pt& a, const Pt& b, double t) { return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t}; }

/// A shape's outline as one polyline (closed outlines repeat the first point).
std::vector<P2> outlineOf(const Shape& s) {
  if (s.kind == "ellipse") {
    std::vector<P2> out;
    for (int i = 0; i <= 96; i++) {
      const double t = (i / 96.0) * M_PI * 2;
      out.push_back({s.cx + std::cos(t) * s.rx, s.cy + std::sin(t) * s.ry});
    }
    return out;
  }
  std::vector<P2> out = s.points;
  if (isClosedShape(s) && !s.points.empty()) out.push_back(s.points[0]);
  return out;
}

}  // namespace

double distToSegment(double px, double py, double ax, double ay, double bx, double by) {
  const double dx = bx - ax, dy = by - ay;
  const double len2 = dx * dx + dy * dy;
  double t = len2 == 0 ? 0 : ((px - ax) * dx + (py - ay) * dy) / len2;
  t = std::max(0.0, std::min(1.0, t));
  return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

bool pointInPolygon(double x, double y, const std::vector<P2>& poly) {
  bool inside = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const double xi = poly[i][0], yi = poly[i][1], xj = poly[j][0], yj = poly[j][1];
    if ((yi > y) != (yj > y) && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

Rect rectOfPts(const std::vector<P2>& pts, double pad) { return rectOf(pts, pad); }

double textHeight(const QString& text, double fontSize, double width, const El& style) {
  const draw::TextStyle ts{style.font.value_or(QString()), style.bold.value_or(false), style.italic.value_or(false), style.align.value_or(QString())};
  const double w = width < 0 ? INF : width;
  return std::max(1.0, double(draw::layoutText(text, 0, 0, w, fontSize, fontSize, ts).size())) * fontSize * 1.35;
}

Rect boxOf(const El& el) {
  if (el.kind == Kind::Text) return {el.x, el.y, el.w, textHeight(el.text, el.fontSize.value_or(0), el.w, el)};
  return {el.x, el.y, el.w, el.h};
}

Rect bounds(const El& el) {
  switch (el.kind) {
    case Kind::Stroke: return rectOf(el.points, el.width / 2);
    case Kind::Shape: {
      const Shape& s = el.shape;
      if (s.kind == "ellipse") return {s.cx - s.rx - el.width / 2, s.cy - s.ry - el.width / 2, 2 * s.rx + el.width, 2 * s.ry + el.width};
      return rectOf(s.points, el.width / 2);
    }
    default: return rotatedBounds(boxOf(el), el.rot());
  }
}

std::optional<Rect> unionRects(const std::vector<Rect>& rects) {
  if (rects.empty()) return std::nullopt;
  double x0 = INF, y0 = INF, x1 = -INF, y1 = -INF;
  for (const Rect& r : rects) {
    x0 = std::min(x0, r.x);
    y0 = std::min(y0, r.y);
    x1 = std::max(x1, r.x + r.w);
    y1 = std::max(y1, r.y + r.h);
  }
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

bool rectContains(const Rect& r, double x, double y) { return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; }

std::vector<std::vector<P2>> shapeParts(const Shape& s) {
  if (s.kind == "ellipse") return {};
  if (s.kind != "path" || s.breaks.empty()) return {s.points};
  std::vector<int> cuts{0};
  cuts.insert(cuts.end(), s.breaks.begin(), s.breaks.end());
  cuts.push_back(int(s.points.size()));
  std::vector<std::vector<P2>> out;
  for (size_t i = 0; i + 1 < cuts.size(); i++)
    if (cuts[i + 1] > cuts[i]) out.emplace_back(s.points.begin() + cuts[i], s.points.begin() + std::min<int>(cuts[i + 1], int(s.points.size())));
  return out;
}

bool isClosedShape(const Shape& s) {
  if (s.kind == "line" || s.kind == "arrow") return false;
  if (s.kind == "path") return s.closed;
  return true;
}

bool hitTest(const El& el, double x, double y, double radius) {
  switch (el.kind) {
    case Kind::Stroke: {
      std::vector<P2> pts;
      pts.reserve(el.points.size());
      for (const Pt& p : el.points) pts.push_back({p[0], p[1]});
      return distToPolyline(x, y, pts, false) <= radius + el.width / 2;
    }
    case Kind::Shape: {
      const Shape& s = el.shape;
      if (s.kind == "ellipse") {
        const double nx = (x - s.cx) / std::max(s.rx, 1e-6), ny = (y - s.cy) / std::max(s.ry, 1e-6);
        const double d = std::abs(std::hypot(nx, ny) - 1) * std::min(s.rx, s.ry);
        return d <= radius + el.width / 2 || (el.fill.has_value() && std::hypot(nx, ny) <= 1);
      }
      const bool closed = isClosedShape(s);
      if (s.kind == "path" && !s.breaks.empty()) {
        for (const auto& part : shapeParts(s))
          if (distToPolyline(x, y, part, false) <= radius + el.width / 2) return true;
        return false;
      }
      if (distToPolyline(x, y, s.points, closed) <= radius + el.width / 2) return true;
      return closed && el.fill.has_value() && pointInPolygon(x, y, s.points);
    }
    default: {
      // Test in the box's own (unrotated) frame.
      const Rect r = boxOf(el);
      const double rot = el.rot();
      double px = x, py = y;
      if (rot) {
        const double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        const double c = std::cos(-rot), s = std::sin(-rot);
        px = cx + (x - cx) * c - (y - cy) * s;
        py = cy + (x - cx) * s + (y - cy) * c;
      }
      return rectContains({r.x - radius, r.y - radius, r.w + 2 * radius, r.h + 2 * radius}, px, py);
    }
  }
}

bool hitForSelect(const El& el, double x, double y, double radius) {
  if (hitTest(el, x, y, radius)) return true;
  if (el.kind != Kind::Shape) return false;
  const Shape& s = el.shape;
  if (s.kind == "ellipse") return std::hypot((x - s.cx) / std::max(s.rx, 1e-6), (y - s.cy) / std::max(s.ry, 1e-6)) <= 1;
  if (!isClosedShape(s)) return false;
  return pointInPolygon(x, y, s.points);
}

Lasso::Lasso(const std::vector<P2>& lasso) {
  if (lasso.size() < 3) return;
  double px = INF, py = INF;
  for (const P2& p : lasso) {
    if (std::abs(p[0] - px) + std::abs(p[1] - py) < 1.5) continue;
    m_xs.push_back(p[0]);
    m_ys.push_back(p[1]);
    px = p[0];
    py = p[1];
  }
}

bool Lasso::inside(double x, double y) const {
  bool in = false;
  const size_t n = m_xs.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double yi = m_ys[i], yj = m_ys[j];
    if ((yi > y) != (yj > y) && x < ((m_xs[j] - m_xs[i]) * (y - yi)) / (yj - yi) + m_xs[i]) in = !in;
  }
  return in;
}

bool Lasso::holds(const El& el) const {
  if (m_xs.size() < 3) return false;
  auto sample = [&](const auto& pts) {
    const size_t n = pts.size();
    if (!n) return false;
    const size_t step = std::max<size_t>(1, n / 96);
    int in = 0, seen = 0;
    for (size_t i = 0; i < n; i += step) {
      seen++;
      if (inside(pts[i][0], pts[i][1])) in++;
    }
    return double(in) / seen >= 0.6;
  };
  if (el.kind == Kind::Stroke) return sample(el.points);
  if (el.kind == Kind::Shape && el.shape.kind != "ellipse") return sample(el.shape.points);
  const Rect r = bounds(el);
  return inside(r.x + r.w / 2, r.y + r.h / 2);
}

El translate(const El& el, double dx, double dy) {
  El out = el;
  switch (el.kind) {
    case Kind::Stroke:
      for (Pt& p : out.points) p = {p[0] + dx, p[1] + dy, p[2]};
      break;
    case Kind::Shape:
      if (el.shape.kind == "ellipse") {
        out.shape.cx += dx;
        out.shape.cy += dy;
      } else {
        for (P2& p : out.shape.points) p = {p[0] + dx, p[1] + dy};
      }
      break;
    default:
      out.x += dx;
      out.y += dy;
  }
  return out;
}

bool isHeld(const std::vector<double>& times, const std::vector<Pt>& pts, double ms, double tolerance) {
  if (pts.size() < 2 || times.size() != pts.size()) return false;
  const double end = times.back();
  const Pt& last = pts.back();
  for (int i = int(pts.size()) - 1; i >= 0; i--)
    if (std::hypot(pts[size_t(i)][0] - last[0], pts[size_t(i)][1] - last[1]) > tolerance) return end - times[size_t(i)] >= ms;
  return end - times[0] >= ms;
}

std::optional<Shape> buildShape(const QString& kind, P2 a, P2 b, bool constrain) {
  double x1 = b[0], y1 = b[1];
  const double x0 = a[0], y0 = a[1];
  if (kind == "auto") return std::nullopt;
  Shape s;
  if (kind == "line" || kind == "arrow") {
    if (constrain) {
      const double len = std::hypot(x1 - x0, y1 - y0);
      const double step = M_PI / 4;
      const double ang = jsRound(std::atan2(y1 - y0, x1 - x0) / step) * step;
      x1 = x0 + std::cos(ang) * len;
      y1 = y0 + std::sin(ang) * len;
    }
    if (std::hypot(x1 - x0, y1 - y0) < 3) return std::nullopt;
    s.kind = kind;
    s.points = {{x0, y0}, {x1, y1}};
    return s;
  }
  double w = x1 - x0, h = y1 - y0;
  if (constrain) {
    const double side = std::max(std::abs(w), std::abs(h));
    w = jsSign(w ? w : 1) * side;
    h = jsSign(h ? h : 1) * side;
  }
  if (std::abs(w) < 3 || std::abs(h) < 3) return std::nullopt;
  const double left = std::min(x0, x0 + w), top = std::min(y0, y0 + h);
  const double right = left + std::abs(w), bottom = top + std::abs(h);
  const double cx = (left + right) / 2, cy = (top + bottom) / 2;
  if (kind == "rectangle") {
    s.kind = "rectangle";
    s.points = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
  } else if (kind == "ellipse") {
    s.kind = "ellipse";
    s.cx = cx;
    s.cy = cy;
    s.rx = (right - left) / 2;
    s.ry = (bottom - top) / 2;
  } else if (kind == "triangle") {
    s.kind = "triangle";
    s.points = {{cx, top}, {right, bottom}, {left, bottom}};
  } else if (kind == "diamond") {
    s.kind = "polygon";
    s.points = {{cx, top}, {right, cy}, {cx, bottom}, {left, cy}};
  } else if (kind == "star") {
    const double rx = (right - left) / 2, ry = (bottom - top) / 2;
    s.kind = "polygon";
    for (int i = 0; i < 10; i++) {
      const double r = i % 2 == 0 ? 1 : 0.42;
      const double t = -M_PI / 2 + (i * M_PI) / 5;
      s.points.push_back({cx + std::cos(t) * rx * r, cy + std::sin(t) * ry * r});
    }
  } else {
    return std::nullopt;
  }
  return s;
}

bool isResizable(const El& el) { return el.kind == Kind::Shape || el.kind == Kind::Image || el.kind == Kind::Sticky || el.kind == Kind::Graph || el.kind == Kind::Table; }

Rect geomBox(const El& el) {
  if (el.kind == Kind::Shape) {
    const Shape& s = el.shape;
    if (s.kind == "ellipse") return {s.cx - s.rx, s.cy - s.ry, 2 * s.rx, 2 * s.ry};
    return rectOf(s.points, 0);
  }
  return bounds(el);
}

std::vector<Handle> handlesFor(const El& el) {
  if (!isResizable(el)) return {};
  if (isSegment(el)) {
    const P2 a = el.shape.points.front(), b = el.shape.points.back();
    return {{"p0", a[0], a[1]}, {"p1", b[0], b[1]}};
  }
  const Rect r = geomBox(el);
  const double x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h, cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  return {{"nw", x0, y0}, {"n", cx, y0}, {"ne", x1, y0}, {"e", x1, cy}, {"se", x1, y1}, {"s", cx, y1}, {"sw", x0, y1}, {"w", x0, cy}};
}

QString handleAt(const El& el, double x, double y, double radius) {
  for (const Handle& h : handlesFor(el))
    if (std::hypot(h.x - x, h.y - y) <= radius) return h.id;
  return {};
}

QString handleCursor(const QString& h) {
  if (h == "nw" || h == "se") return "nwse-resize";
  if (h == "ne" || h == "sw") return "nesw-resize";
  if (h == "n" || h == "s") return "ns-resize";
  if (h == "e" || h == "w") return "ew-resize";
  return "move";
}

El resizeElement(const El& el, const QString& handle, P2 p, bool keepAspect) {
  if (isSegment(el)) {
    El out = el;
    std::vector<P2>& pts = out.shape.points;
    const size_t idx = handle == "p0" ? 0 : pts.size() - 1;
    const P2 other = pts[handle == "p0" ? pts.size() - 1 : 0];
    double x = p[0], y = p[1];
    if (keepAspect) {
      const double len = std::hypot(x - other[0], y - other[1]);
      const double step = M_PI / 4;
      const double ang = jsRound(std::atan2(y - other[1], x - other[0]) / step) * step;
      x = other[0] + std::cos(ang) * len;
      y = other[1] + std::sin(ang) * len;
    }
    pts[idx] = {x, y};
    return out;
  }
  if (!isResizable(el) || handle == "p0" || handle == "p1") return el;
  const Rect box = geomBox(el);
  double x0 = box.x, y0 = box.y, x1 = box.x + box.w, y1 = box.y + box.h;
  if (handle.contains('w')) x0 = std::min(p[0], x1 - MIN_SIZE);
  if (handle.contains('e')) x1 = std::max(p[0], x0 + MIN_SIZE);
  if (handle.contains('n')) y0 = std::min(p[1], y1 - MIN_SIZE);
  if (handle.contains('s')) y1 = std::max(p[1], y0 + MIN_SIZE);
  if (keepAspect && handle.size() == 2 && box.w > 0 && box.h > 0) {
    const double scale = std::max((x1 - x0) / box.w, (y1 - y0) / box.h);
    const double w = box.w * scale, hh = box.h * scale;
    if (handle.contains('w')) x0 = x1 - w;
    else x1 = x0 + w;
    if (handle.contains('n')) y0 = y1 - hh;
    else y1 = y0 + hh;
  }
  const double sx = box.w > 0 ? (x1 - x0) / box.w : 1, sy = box.h > 0 ? (y1 - y0) / box.h : 1;
  El out = el;
  if (el.kind == Kind::Shape) {
    if (el.shape.kind == "ellipse") {
      out.shape.cx = (x0 + x1) / 2;
      out.shape.cy = (y0 + y1) / 2;
      out.shape.rx = (x1 - x0) / 2;
      out.shape.ry = (y1 - y0) / 2;
    } else {
      for (P2& q : out.shape.points) q = {x0 + (q[0] - box.x) * sx, y0 + (q[1] - box.y) * sy};
    }
    return out;
  }
  if (el.kind == Kind::Image || el.kind == Kind::Sticky || el.kind == Kind::Graph || el.kind == Kind::Table) {
    out.x = x0;
    out.y = y0;
    out.w = x1 - x0;
    out.h = y1 - y0;
  }
  return out;
}

El convertShape(const El& el, const QString& kind) {
  if (el.kind != Kind::Shape || kind == "auto") return el;
  El out = el;
  if (isSegment(el)) {
    if (kind == "line" || kind == "arrow") {
      out.shape = Shape();
      out.shape.kind = kind;
      out.shape.points = el.shape.points;
    }
    return out;
  }
  if (kind == "line" || kind == "arrow") return el;
  const Rect r = geomBox(el);
  if (auto s = buildShape(kind, {r.x, r.y}, {r.x + r.w, r.y + r.h})) out.shape = *s;
  return out;
}

Shape transformShape(const Shape& s, P2 pivot, double angle, double scale, double dx, double dy) {
  const double cos = std::cos(angle) * scale, sin = std::sin(angle) * scale;
  auto map = [&](P2 p) -> P2 { return {pivot[0] + (p[0] - pivot[0]) * cos - (p[1] - pivot[1]) * sin + dx, pivot[1] + (p[0] - pivot[0]) * sin + (p[1] - pivot[1]) * cos + dy}; };
  Shape out = s;
  if (s.kind == "ellipse") {
    const P2 c = map({s.cx, s.cy});
    const bool circle = std::abs(s.rx - s.ry) < 0.5;
    if (circle || std::abs(angle) < 0.035) {
      out.cx = c[0];
      out.cy = c[1];
      out.rx = s.rx * scale;
      out.ry = s.ry * scale;
      return out;
    }
    Shape path;
    path.kind = "path";
    path.closed = true;
    for (int i = 0; i < 72; i++) {
      const double t = (i / 72.0) * M_PI * 2;
      path.points.push_back(map({s.cx + std::cos(t) * s.rx, s.cy + std::sin(t) * s.ry}));
    }
    path.points.push_back(path.points[0]);
    return path;
  }
  for (P2& p : out.points) p = map(p);
  return out;
}

std::optional<std::vector<El>> eraseElement(const El& el, double x, double y, double radius) {
  if (el.kind == Kind::Stroke) {
    const double reach = radius + el.width / 2;
    const Rect r = bounds(el);
    if (x < r.x - reach || x > r.x + r.w + reach || y < r.y - reach || y > r.y + r.h + reach) return std::nullopt;
    if (el.points.size() == 1) {
      if (std::hypot(el.points[0][0] - x, el.points[0][1] - y) <= reach) return std::vector<El>{};
      return std::nullopt;
    }
    auto runs = cutPolyline(el.points, x, y, reach, lerp3);
    if (!runs) return std::nullopt;
    std::vector<El> out;
    for (size_t i = 0; i < runs->size(); ++i) {
      El piece = el;
      piece.id = i == 0 ? el.id : newId();
      piece.points = (*runs)[i];
      out.push_back(piece);
    }
    return out;
  }
  if (el.kind == Kind::Shape && el.shape.kind == "path" && !el.shape.breaks.empty()) {
    // A multi-stroke symbol: cut each of its strokes on its own.
    const double reach = radius + el.width / 2;
    bool touched = false;
    std::vector<std::vector<P2>> pieces;
    for (const auto& part : shapeParts(el.shape)) {
      auto runs = cutPolyline(part, x, y, reach, lerp2);
      if (runs) {
        touched = true;
        pieces.insert(pieces.end(), runs->begin(), runs->end());
      } else {
        pieces.push_back(part);
      }
    }
    if (!touched) return std::nullopt;
    std::vector<El> out;
    for (const auto& pts : pieces) {
      if (pts.size() <= 1) continue;
      El piece = el;
      piece.id = out.empty() ? el.id : newId();
      piece.shape = Shape();
      piece.shape.kind = "path";
      piece.shape.points = pts;
      piece.fill.reset();
      out.push_back(piece);
    }
    return out;
  }
  if (el.kind == Kind::Shape) {
    const double reach = radius + el.width / 2;
    auto runs = cutPolyline(outlineOf(el.shape), x, y, reach, lerp2);
    if (!runs) return std::nullopt;
    const bool arrow = el.shape.kind == "arrow";
    const P2 tip = arrow ? el.shape.points.back() : P2{0, 0};
    std::vector<El> out;
    for (size_t i = 0; i < runs->size(); ++i) {
      const auto& pts = (*runs)[i];
      const P2 last = pts.back();
      const bool keepsTip = arrow && std::hypot(last[0] - tip[0], last[1] - tip[1]) < 1e-6;
      El piece = el;
      piece.id = i == 0 ? el.id : newId();
      piece.shape = Shape();
      piece.shape.kind = keepsTip ? "arrow" : "path";
      piece.shape.points = pts;
      piece.fill.reset();
      out.push_back(piece);
    }
    return out;
  }
  return std::nullopt;
}

P2 applyAffine(const Affine& m, P2 p) { return {m.a * p[0] + m.c * p[1] + m.e, m.b * p[0] + m.d * p[1] + m.f}; }
Affine scaleAbout(double ax, double ay, double sx, double sy) { return {sx, 0, 0, sy, ax - sx * ax, ay - sy * ay}; }
Affine rotateAbout(double cx, double cy, double angle) {
  const double cos = std::cos(angle), sin = std::sin(angle);
  return {cos, sin, -sin, cos, cx - cos * cx + sin * cy, cy - sin * cx - cos * cy};
}

std::vector<Handle> groupHandles(const Rect& box, double zoom) {
  const double x0 = box.x, y0 = box.y, x1 = box.x + box.w, y1 = box.y + box.h;
  const double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  return {{"nw", x0, y0}, {"n", cx, y0}, {"ne", x1, y0}, {"e", x1, cy}, {"se", x1, y1}, {"s", cx, y1}, {"sw", x0, y1}, {"w", x0, cy}, {"rot", cx, y1 + 30 / zoom}};
}

QString groupHandleAt(const Rect& box, double x, double y, double zoom) {
  for (const Handle& h : groupHandles(box, zoom))
    if (std::hypot(h.x - x, h.y - y) <= 10 / zoom) return h.id;
  return {};
}

GroupTransform groupTransform(const Rect& box, const QString& handle, P2 start, P2 p, bool constrain) {
  if (handle == "rot") {
    const double cx = box.x + box.w / 2, cy = box.y + box.h / 2;
    double angle = std::atan2(p[1] - cy, p[0] - cx) - std::atan2(start[1] - cy, start[0] - cx);
    if (constrain) angle = jsRound(angle / (M_PI / 12)) * (M_PI / 12);
    return {rotateAbout(cx, cy, angle), 1, 1, angle};
  }
  double x0 = box.x, y0 = box.y, x1 = box.x + box.w, y1 = box.y + box.h;
  if (handle.contains('w')) x0 = std::min(p[0], x1 - MIN_SIZE);
  if (handle.contains('e')) x1 = std::max(p[0], x0 + MIN_SIZE);
  if (handle.contains('n')) y0 = std::min(p[1], y1 - MIN_SIZE);
  if (handle.contains('s')) y1 = std::max(p[1], y0 + MIN_SIZE);
  double sx = box.w > 0 ? (x1 - x0) / box.w : 1, sy = box.h > 0 ? (y1 - y0) / box.h : 1;
  if (constrain || handle.size() == 2) {
    // Corners keep proportions by default; Shift does it for edges too.
    const double s = handle.size() == 2 ? std::max(sx, sy) : handle == "n" || handle == "s" ? sy : sx;
    sx = s;
    sy = s;
  }
  const double ax = handle.contains('w') ? box.x + box.w : handle.contains('e') ? box.x : box.x + box.w / 2;
  const double ay = handle.contains('n') ? box.y + box.h : handle.contains('s') ? box.y : box.y + box.h / 2;
  return {scaleAbout(ax, ay, sx, sy), sx, sy, 0};
}

El transformElement(const El& el, const Affine& m, double sx, double sy, double angle) {
  const double widthScale = std::sqrt(std::abs(sx * sy));
  El out = el;
  switch (el.kind) {
    case Kind::Stroke:
      out.width = el.width * widthScale;
      for (Pt& p : out.points) {
        const P2 q = applyAffine(m, {p[0], p[1]});
        p = {q[0], q[1], p[2]};
      }
      return out;
    case Kind::Shape: {
      const Shape& s = el.shape;
      if (s.kind == "ellipse") {
        const P2 c = applyAffine(m, {s.cx, s.cy});
        const bool circle = std::abs(s.rx - s.ry) < 0.5;
        if (!angle || circle) {
          out.shape.cx = c[0];
          out.shape.cy = c[1];
          out.shape.rx = s.rx * std::abs(sx);
          out.shape.ry = s.ry * std::abs(sy);
        } else {
          Shape path;
          path.kind = "path";
          path.closed = true;
          for (int i = 0; i < 72; i++) {
            const double t = (i / 72.0) * M_PI * 2;
            path.points.push_back(applyAffine(m, {s.cx + std::cos(t) * s.rx, s.cy + std::sin(t) * s.ry}));
          }
          path.points.push_back(path.points[0]);
          out.shape = path;
        }
      } else {
        for (P2& p : out.shape.points) p = applyAffine(m, p);
      }
      out.width = el.width * widthScale;
      return out;
    }
    default: {
      const Rect r = boxOf(el);
      const P2 c = applyAffine(m, {r.x + r.w / 2, r.y + r.h / 2});
      const double w = r.w * std::abs(sx), h = r.h * std::abs(sy);
      out.rotation = el.rot() + angle;
      out.x = c[0] - w / 2;
      out.y = c[1] - h / 2;
      out.w = w;
      if (el.kind == Kind::Text) {
        out.fontSize = std::max(4.0, el.fontSize.value_or(0) * std::abs(sy));
      } else if (el.kind == Kind::Sticky) {
        out.h = h;
        out.fontSize = std::max(4.0, el.fontSize.value_or(18) * std::abs(sy));
      } else {
        out.h = h;
      }
      return out;
    }
  }
}

}  // namespace ed
