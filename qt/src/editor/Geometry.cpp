#include "Geometry.h"

#include <cmath>
#include <limits>

#include "../draw/Elements.h"

namespace ed {
bool isSegmentKind(const QString& k) { return k == "line" || k == "arrow" || k == "doubleArrow"; }
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

bool isSegment(const El& el) { return el.kind == Kind::Shape && isSegmentKind(el.shape.kind); }

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
    case Kind::Stroke: {
      // How far the ink reaches from the line (the wide and glowing pens further).
      const QString style = el.style.value_or(QString());
      const double reach = style == "watercolor" ? 2.1 : style == "neon" ? 1.7 : style == "marker" ? 1.2 : 0.5;
      return rectOf(el.points, el.width * reach);
    }
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
  if (isSegmentKind(s.kind)) return false;
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
  if (isSegmentKind(kind)) {
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
  } else if (kind == "roundRect" || kind == "bubble") {
    // Rounded corners (quarter circles); a speech bubble has a tail at the bottom left.
    const double bodyBottom = kind == "bubble" ? top + (bottom - top) * 0.78 : bottom;
    const double r = std::min(right - left, bodyBottom - top) * 0.2;
    s.kind = "polygon";
    auto arc = [&](double ox, double oy, double from) {
      for (int i = 0; i <= 8; i++) {
        const double t = from + i * (M_PI / 2) / 8;
        s.points.push_back({ox + std::cos(t) * r, oy + std::sin(t) * r});
      }
    };
    arc(left + r, top + r, M_PI);
    arc(right - r, top + r, -M_PI / 2);
    arc(right - r, bodyBottom - r, 0);
    if (kind == "bubble") {
      const double w = right - left;
      s.points.push_back({left + w * 0.4, bodyBottom});
      s.points.push_back({left + w * 0.14, bottom});
      s.points.push_back({left + w * 0.24, bodyBottom});
    }
    arc(left + r, bodyBottom - r, M_PI / 2);
  } else {
    // The rest are drawn in a unit square, then fitted to the box.
    std::vector<P2> unit;
    auto regular = [&](int n, double start) {
      for (int i = 0; i < n; i++) unit.push_back({std::cos(start + i * 2 * M_PI / n), std::sin(start + i * 2 * M_PI / n)});
    };
    if (kind == "rightTriangle") unit = {{0, 0}, {1, 1}, {0, 1}};
    else if (kind == "parallelogram") unit = {{0.25, 0}, {1, 0}, {0.75, 1}, {0, 1}};
    else if (kind == "trapezoid") unit = {{0.22, 0}, {0.78, 0}, {1, 1}, {0, 1}};
    else if (kind == "pentagon") regular(5, -M_PI / 2);
    else if (kind == "hexagon") regular(6, 0);
    else if (kind == "octagon") regular(8, M_PI / 8);
    else if (kind == "cross") {
      const double a = 1.0 / 3, b = 2.0 / 3;
      unit = {{a, 0}, {b, 0}, {b, a}, {1, a}, {1, b}, {b, b}, {b, 1}, {a, 1}, {a, b}, {0, b}, {0, a}, {a, a}};
    } else if (kind == "blockArrow") unit = {{0, 0.3}, {0.6, 0.3}, {0.6, 0}, {1, 0.5}, {0.6, 1}, {0.6, 0.7}, {0, 0.7}};
    else if (kind == "lightning") unit = {{0.56, 0}, {0.12, 0.58}, {0.44, 0.58}, {0.3, 1}, {0.88, 0.38}, {0.55, 0.38}, {0.74, 0}};
    else if (kind == "heart") {
      for (int i = 0; i < 96; i++) {
        const double t = i * 2 * M_PI / 96;
        unit.push_back({16 * std::pow(std::sin(t), 3), -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t))});
      }
    } else if (kind == "cloud") {
      // Round puffs all the way round (an ellipse with bumps).
      for (int i = 0; i < 120; i++) {
        const double t = i * 2 * M_PI / 120;
        const double r = 0.82 + 0.18 * std::sqrt(std::abs(std::sin(t * 4)));
        unit.push_back({std::cos(t) * r, std::sin(t) * r * 0.78});
      }
    } else if (kind == "moon") {
      // A crescent: the outer circle's arc round the left, then a second circle
      // (as big, moved right) back between the two points where they cross.
      const double d = 0.6, cx2 = d / 2, cy2 = std::sqrt(1 - cx2 * cx2);
      const double from = std::atan2(-cy2, cx2), sweep = 2 * M_PI - 2 * std::abs(from);
      for (int i = 0; i <= 48; i++) {
        const double t = from - sweep * i / 48;
        unit.push_back({std::cos(t), std::sin(t)});
      }
      const double in0 = std::atan2(cy2, cx2 - d), in1 = std::atan2(-cy2, cx2 - d) + 2 * M_PI;
      for (int i = 1; i < 48; i++) {
        const double t = in0 + (in1 - in0) * i / 48;
        unit.push_back({d + std::cos(t), std::sin(t)});
      }
    } else return std::nullopt;
    double ux0 = 1e9, uy0 = 1e9, ux1 = -1e9, uy1 = -1e9;
    for (const P2& p : unit) {
      ux0 = std::min(ux0, p[0]), uy0 = std::min(uy0, p[1]);
      ux1 = std::max(ux1, p[0]), uy1 = std::max(uy1, p[1]);
    }
    s.kind = "polygon";
    for (const P2& p : unit) s.points.push_back({left + (p[0] - ux0) / std::max(1e-9, ux1 - ux0) * (right - left), top + (p[1] - uy0) / std::max(1e-9, uy1 - uy0) * (bottom - top)});
  }
  // Which shape it is, when its kind doesn't say (a polygon): to show it as chosen, and to change it.
  if (s.kind != kind) s.name = kind;
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
    if (isSegmentKind(kind)) {
      out.shape = Shape();
      out.shape.kind = kind;
      out.shape.points = el.shape.points;
    }
    return out;
  }
  if (isSegmentKind(kind)) return el;
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
    // An arrow's heads stay on the pieces that keep its ends.
    const bool headAtEnd = el.shape.kind == "arrow" || el.shape.kind == "doubleArrow";
    const bool headAtStart = el.shape.kind == "doubleArrow";
    const P2 tip = el.shape.points.empty() ? P2{0, 0} : el.shape.points.back();
    const P2 tail = el.shape.points.empty() ? P2{0, 0} : el.shape.points.front();
    std::vector<El> out;
    for (size_t i = 0; i < runs->size(); ++i) {
      auto pts = (*runs)[i];
      const bool keepsTip = headAtEnd && std::hypot(pts.back()[0] - tip[0], pts.back()[1] - tip[1]) < 1e-6;
      const bool keepsTail = headAtStart && std::hypot(pts.front()[0] - tail[0], pts.front()[1] - tail[1]) < 1e-6;
      El piece = el;
      piece.id = i == 0 ? el.id : newId();
      piece.shape = Shape();
      if (keepsTail && !keepsTip) std::reverse(pts.begin(), pts.end());
      piece.shape.kind = keepsTip && keepsTail ? "doubleArrow" : keepsTip || keepsTail ? "arrow" : "path";
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

// --- drafting --------------------------------------------------------------------

namespace ed {

/// A pen stroke that's a straight line (a held line drawn with a pen that keeps its look): its two ends.
std::optional<std::pair<P2, P2>> straightStroke(const El& e) {
  if (e.kind != Kind::Stroke || e.points.size() < 2) return std::nullopt;
  const P2 a{e.points.front()[0], e.points.front()[1]}, b{e.points.back()[0], e.points.back()[1]};
  const double len = std::hypot(b[0] - a[0], b[1] - a[1]);
  if (len < 20) return std::nullopt;
  const double tol = std::max(1.5, len * 0.01);
  for (const Pt& p : e.points)
    if (std::abs((b[0] - a[0]) * (a[1] - p[1]) - (a[0] - p[0]) * (b[1] - a[1])) / len > tol) return std::nullopt;
  return std::pair{a, b};
}

std::vector<DraftLine> draftingLines(const std::vector<ElPtr>& els, const QString& skip) {
  std::vector<DraftLine> out;
  for (const ElPtr& e : els) {
    if (e && e->id != skip)
      if (auto st = straightStroke(*e)) out.push_back({st->first, st->second, e->id});
    if (!e || e->kind != Kind::Shape || e->id == skip) continue;
    const Shape& s = e->shape;
    if (s.kind == "ellipse" || s.name == "point" || s.points.size() < 2) continue;
    if (isSegmentKind(s.kind)) {
      out.push_back({s.points.front(), s.points.back(), e->id});
      continue;
    }
    // A polygon's straight sides (not the many short ones of a curve).
    if (s.kind == "path" || s.points.size() > 12) continue;
    for (size_t i = 0; i < s.points.size(); ++i) out.push_back({s.points[i], s.points[(i + 1) % s.points.size()], e->id});
    if (out.size() > 400) break;
  }
  return out;
}

std::vector<P2> draftingCorners(const std::vector<ElPtr>& els, const QString& skip) {
  std::vector<P2> out;
  for (const ElPtr& e : els) {
    if (e && e->id != skip)
      if (auto st = straightStroke(*e)) out.push_back(st->first), out.push_back(st->second);
    if (!e || e->kind != Kind::Shape || e->id == skip) continue;
    const Shape& s = e->shape;
    if (s.name == "point") out.push_back({s.cx, s.cy});
    else if (isSegmentKind(s.kind) && s.points.size() >= 2) out.push_back(s.points.front()), out.push_back(s.points.back());
    else if (s.kind != "ellipse" && s.kind != "path" && s.points.size() <= 12) out.insert(out.end(), s.points.begin(), s.points.end());
    if (out.size() > 800) break;
  }
  return out;
}

std::optional<P2> nearestCorner(const std::vector<P2>& corners, P2 p, double radius) {
  std::optional<P2> best;
  double bestD = radius;
  for (const P2& c : corners) {
    const double d = std::hypot(c[0] - p[0], c[1] - p[1]);
    if (d <= bestD) bestD = d, best = c;
  }
  return best;
}

namespace {
double distToSeg(P2 p, P2 a, P2 b) {
  const double dx = b[0] - a[0], dy = b[1] - a[1], l2 = dx * dx + dy * dy;
  const double t = l2 > 0 ? std::clamp(((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2, 0.0, 1.0) : 0;
  return std::hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy));
}
/// The angle a line makes (radians), folded to [0, π): a line has no direction.
double lineAngle(const DraftLine& s) {
  double a = std::atan2(s.b[1] - s.a[1], s.b[0] - s.a[0]);
  while (a < 0) a += M_PI;
  while (a >= M_PI) a -= M_PI;
  return a;
}
/// How far `a` (a direction) is from `target` (a line's angle), either way along it, in radians.
double offLine(double a, double target) {
  double d = std::fmod(a - target, M_PI);
  if (d > M_PI / 2) d -= M_PI;
  if (d < -M_PI / 2) d += M_PI;
  return d;
}
}  // namespace

std::optional<DraftLine> lineThrough(const std::vector<DraftLine>& lines, P2 p, double radius) {
  std::optional<DraftLine> best;
  double bestD = radius;
  for (const DraftLine& s : lines) {
    const double d = distToSeg(p, s.a, s.b);
    if (d <= bestD) bestD = d, best = s;
  }
  return best;
}

AngleSnap snapDirection(double angle, const std::vector<DraftLine>& lines, const std::optional<DraftLine>& startsOn, double toleranceDeg) {
  // Lines drawn by hand are a little off: square and parallel reach further (2×) than round angles.
  const double tol = toleranceDeg * M_PI / 180;
  AngleSnap best{angle, "", std::nullopt};
  double bestScore = 1;  // how far off, as a share of what that kind allows
  auto consider = [&](double target, const QString& why, const std::optional<DraftLine>& ref, double reach) {
    const double off = offLine(angle, target);
    const double score = std::abs(off) / (tol * reach);
    if (score < bestScore) {
      bestScore = score;
      best = {angle - off, why, ref};
    }
  };
  if (startsOn) consider(lineAngle(*startsOn) + M_PI / 2, "square", startsOn, 2.0);
  int n = 0;
  for (const DraftLine& s : lines) {
    if (std::hypot(s.b[0] - s.a[0], s.b[1] - s.a[1]) < 8) continue;
    consider(lineAngle(s), "parallel", s, 2.0);
    if (++n > 200) break;
  }
  for (int k = 0; k < 12; ++k) consider(k * M_PI / 12, "round", std::nullopt, 1.0);
  return best;
}

double angleBetween(const DraftLine& s, P2 from, P2 to) {
  const double a = std::atan2(to[1] - from[1], to[0] - from[0]);
  double d = std::abs(offLine(a, lineAngle(s)));  // 0 … π/2
  d = d * 180 / M_PI;
  // Which side of the other line: if it starts at one of its ends, the angle
  // inside (towards the rest of that line), which can be more than 90°.
  const auto at = [&](P2 q) { return std::hypot(q[0] - from[0], q[1] - from[1]) < 1e-6 + 0.5; };
  if (at(s.a) || at(s.b)) {
    const P2 other = at(s.a) ? s.b : s.a;
    const double b = std::atan2(other[1] - from[1], other[0] - from[0]);
    double diff = std::abs(a - b);
    if (diff > M_PI) diff = 2 * M_PI - diff;
    return diff * 180 / M_PI;
  }
  return d;
}

bool squareUp(Shape& s, double tolerance) {
  if (s.kind == "ellipse") {
    const double m = std::max(s.rx, s.ry);
    if (m <= 0 || std::abs(s.rx - s.ry) / m > tolerance) return false;
    s.rx = s.ry = (s.rx + s.ry) / 2;
    return true;
  }
  if (s.kind != "rectangle" || s.points.size() != 4) return false;
  const P2 p0 = s.points[0], p1 = s.points[1], p3 = s.points[3];
  const double a = std::hypot(p1[0] - p0[0], p1[1] - p0[1]), b = std::hypot(p3[0] - p0[0], p3[1] - p0[1]);
  if (a <= 0 || b <= 0 || std::abs(a - b) / std::max(a, b) > tolerance) return false;
  const double side = (a + b) / 2;
  const P2 u{(p1[0] - p0[0]) / a, (p1[1] - p0[1]) / a}, v{(p3[0] - p0[0]) / b, (p3[1] - p0[1]) / b};
  const P2 c{(s.points[0][0] + s.points[2][0]) / 2, (s.points[0][1] + s.points[2][1]) / 2};
  const double h = side / 2;
  s.points = {{c[0] - u[0] * h - v[0] * h, c[1] - u[1] * h - v[1] * h}, {c[0] + u[0] * h - v[0] * h, c[1] + u[1] * h - v[1] * h},
              {c[0] + u[0] * h + v[0] * h, c[1] + u[1] * h + v[1] * h}, {c[0] - u[0] * h + v[0] * h, c[1] - u[1] * h + v[1] * h}};
  return true;
}

std::pair<double, double> shapeSize(const Shape& s) {
  if (s.kind == "ellipse") return {2 * s.rx, 2 * s.ry};
  if (s.kind == "rectangle" && s.points.size() == 4)
    return {std::hypot(s.points[1][0] - s.points[0][0], s.points[1][1] - s.points[0][1]), std::hypot(s.points[3][0] - s.points[0][0], s.points[3][1] - s.points[0][1])};
  const Rect r = rectOf(s.points, 0);
  return {r.w, r.h};
}

namespace {
/// The circle through three points, if they aren't in a line.
std::optional<std::pair<P2, double>> circleThrough(P2 a, P2 b, P2 c) {
  const double d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1]));
  if (std::abs(d) < 1e-9) return std::nullopt;
  const double a2 = a[0] * a[0] + a[1] * a[1], b2 = b[0] * b[0] + b[1] * b[1], c2 = c[0] * c[0] + c[1] * c[1];
  const P2 o{(a2 * (b[1] - c[1]) + b2 * (c[1] - a[1]) + c2 * (a[1] - b[1])) / d, (a2 * (c[0] - b[0]) + b2 * (a[0] - c[0]) + c2 * (b[0] - a[0])) / d};
  return std::pair{o, std::hypot(a[0] - o[0], a[1] - o[1])};
}
}  // namespace

std::optional<Shape> fitArc(const std::vector<P2>& pts) {
  if (pts.size() < 8) return std::nullopt;
  // The best circle through all the points (least squares, Kåsa's method).
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sxz = 0, syz = 0, sz = 0;
  const double n = double(pts.size());
  for (const P2& p : pts) {
    const double z = p[0] * p[0] + p[1] * p[1];
    sx += p[0], sy += p[1], sxx += p[0] * p[0], syy += p[1] * p[1], sxy += p[0] * p[1], sxz += p[0] * z, syz += p[1] * z, sz += z;
  }
  // Solve for x² + y² + D·x + E·y + F = 0.
  const double m[3][4] = {{sxx, sxy, sx, -sxz}, {sxy, syy, sy, -syz}, {sx, sy, n, -sz}};
  double a[3][4];
  std::copy(&m[0][0], &m[0][0] + 12, &a[0][0]);
  for (int i = 0; i < 3; ++i) {
    int piv = i;
    for (int k = i + 1; k < 3; ++k)
      if (std::abs(a[k][i]) > std::abs(a[piv][i])) piv = k;
    if (std::abs(a[piv][i]) < 1e-12) return std::nullopt;
    for (int j = 0; j < 4; ++j) std::swap(a[i][j], a[piv][j]);
    for (int k = 0; k < 3; ++k) {
      if (k == i) continue;
      const double f = a[k][i] / a[i][i];
      for (int j = i; j < 4; ++j) a[k][j] -= f * a[i][j];
    }
  }
  const double D = a[0][3] / a[0][0], E = a[1][3] / a[1][1], F = a[2][3] / a[2][2];
  P2 c{-D / 2, -E / 2};
  const double r2 = c[0] * c[0] + c[1] * c[1] - F;
  if (r2 <= 0) return std::nullopt;
  double r = std::sqrt(r2);
  // A few steps towards the circle closest to the points themselves (that estimate
  // runs small on a short, wobbly arc).
  for (int it = 0; it < 30; ++it) {
    double mr = 0;
    for (const P2& p : pts) mr += std::hypot(p[0] - c[0], p[1] - c[1]);
    mr /= n;
    P2 next{0, 0};
    for (const P2& p : pts) {
      const double d = std::max(1e-9, std::hypot(p[0] - c[0], p[1] - c[1]));
      next[0] += p[0] - mr * (p[0] - c[0]) / d;
      next[1] += p[1] - mr * (p[1] - c[1]) / d;
    }
    c = {next[0] / n, next[1] / n};
    r = mr;
  }
  // Round enough: every point close to the circle.
  double worst = 0;
  for (const P2& p : pts) worst = std::max(worst, std::abs(std::hypot(p[0] - c[0], p[1] - c[1]) - r));
  if (worst > std::max(2.0, r * 0.09) || r > 4000) return std::nullopt;
  // How far round it goes (always the same way).
  double sweep = 0, prev = std::atan2(pts[0][1] - c[1], pts[0][0] - c[0]);
  for (size_t i = 1; i < pts.size(); ++i) {
    const double t = std::atan2(pts[i][1] - c[1], pts[i][0] - c[0]);
    double d = t - prev;
    if (d > M_PI) d -= 2 * M_PI;
    if (d < -M_PI) d += 2 * M_PI;
    sweep += d;
    prev = t;
  }
  const double deg = std::abs(sweep) * 180 / M_PI;
  if (deg < 25 || deg > 330) return std::nullopt;
  // Not a flat curve that's really a line (that's the line's job).
  const P2 a0 = pts.front(), a1 = pts.back();
  const double chord = std::hypot(a1[0] - a0[0], a1[1] - a0[1]);
  if (chord > 0 && r - std::sqrt(std::max(0.0, r * r - chord * chord / 4)) < chord * 0.06 && deg < 90) return std::nullopt;
  Shape s;
  s.kind = "path";
  s.name = "arc";
  s.closed = false;
  const double from = std::atan2(a0[1] - c[1], a0[0] - c[0]);
  const int steps = std::clamp(int(deg / 3), 12, 120);
  for (int i = 0; i <= steps; ++i) {
    const double t = from + sweep * i / steps;
    s.points.push_back({c[0] + std::cos(t) * r, c[1] + std::sin(t) * r});
  }
  return s;
}

std::optional<ArcInfo> arcInfo(const Shape& s) {
  if (s.name != "arc" || s.points.size() < 3) return std::nullopt;
  const P2 a = s.points.front(), m = s.points[s.points.size() / 2], b = s.points.back();
  const auto circle = circleThrough(a, m, b);
  if (!circle) return std::nullopt;
  const auto [c, r] = *circle;
  double sweep = 0, prev = std::atan2(a[1] - c[1], a[0] - c[0]);
  for (size_t i = 1; i < s.points.size(); ++i) {
    const double t = std::atan2(s.points[i][1] - c[1], s.points[i][0] - c[0]);
    double d = t - prev;
    if (d > M_PI) d -= 2 * M_PI;
    if (d < -M_PI) d += 2 * M_PI;
    sweep += d;
    prev = t;
  }
  return ArcInfo{c, r, -sweep * 180 / M_PI};
}

bool isMarkKind(const QString& kind) { return kind == "tick1" || kind == "tick2" || kind == "tick3" || kind == "rightMark" || kind == "angleMark"; }

std::optional<Shape> markShape(const QString& kind, const std::vector<DraftLine>& lines, P2 p, double reach, double size) {
  Shape s;
  s.kind = "path";
  s.name = "mark";
  if (kind.startsWith("tick")) {
    // Across the middle of the nearest side, one to three short strokes.
    std::optional<DraftLine> side = lineThrough(lines, p, reach);
    if (!side) return std::nullopt;
    const double len = std::hypot(side->b[0] - side->a[0], side->b[1] - side->a[1]);
    if (len < 1e-6) return std::nullopt;
    const P2 u{(side->b[0] - side->a[0]) / len, (side->b[1] - side->a[1]) / len}, nrm{-u[1], u[0]};
    const P2 mid{(side->a[0] + side->b[0]) / 2, (side->a[1] + side->b[1]) / 2};
    const int n = kind == "tick3" ? 3 : kind == "tick2" ? 2 : 1;
    const double gap = size * 0.35;
    for (int k = 0; k < n; ++k) {
      const double off = (k - (n - 1) / 2.0) * gap;
      if (k > 0) s.breaks.push_back(int(s.points.size()));
      s.points.push_back({mid[0] + u[0] * off - nrm[0] * size / 2, mid[1] + u[1] * off - nrm[1] * size / 2});
      s.points.push_back({mid[0] + u[0] * off + nrm[0] * size / 2, mid[1] + u[1] * off + nrm[1] * size / 2});
    }
    return s;
  }
  // A corner: two lines that meet at an end, the nearest to the tap.
  std::optional<std::tuple<P2, P2, P2>> best;  // corner, the far ends of the two arms
  double bestD = reach * 2;
  auto same = [](P2 a, P2 b) { return std::hypot(a[0] - b[0], a[1] - b[1]) < 1.0; };
  for (size_t i = 0; i < lines.size(); ++i)
    for (size_t j = i + 1; j < lines.size(); ++j)
      for (const auto& [ci, fi] : {std::pair{lines[i].a, lines[i].b}, std::pair{lines[i].b, lines[i].a}})
        for (const auto& [cj, fj] : {std::pair{lines[j].a, lines[j].b}, std::pair{lines[j].b, lines[j].a}}) {
          if (!same(ci, cj)) continue;
          const double d = std::hypot(ci[0] - p[0], ci[1] - p[1]);
          if (d < bestD) bestD = d, best = std::tuple{ci, fi, fj};
        }
  if (!best) return std::nullopt;
  const auto [c, f1, f2] = *best;
  const double l1 = std::hypot(f1[0] - c[0], f1[1] - c[1]), l2 = std::hypot(f2[0] - c[0], f2[1] - c[1]);
  if (l1 < 1e-6 || l2 < 1e-6) return std::nullopt;
  const P2 u1{(f1[0] - c[0]) / l1, (f1[1] - c[1]) / l1}, u2{(f2[0] - c[0]) / l2, (f2[1] - c[1]) / l2};
  const double r = std::min({size, l1 * 0.4, l2 * 0.4});
  if (kind == "rightMark") {
    s.points = {{c[0] + u1[0] * r, c[1] + u1[1] * r}, {c[0] + (u1[0] + u2[0]) * r, c[1] + (u1[1] + u2[1]) * r}, {c[0] + u2[0] * r, c[1] + u2[1] * r}};
    return s;
  }
  double t0 = std::atan2(u1[1], u1[0]), sweep = std::atan2(u2[1], u2[0]) - t0;
  while (sweep > M_PI) sweep -= 2 * M_PI;
  while (sweep < -M_PI) sweep += 2 * M_PI;
  const double ar = r * 1.3;
  for (int i = 0; i <= 24; ++i) s.points.push_back({c[0] + std::cos(t0 + sweep * i / 24) * ar, c[1] + std::sin(t0 + sweep * i / 24) * ar});
  return s;
}

}  // namespace ed
