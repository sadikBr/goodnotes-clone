#include "Ink.h"

#include <algorithm>
#include <cmath>

namespace ink {

Pen penFromName(const QString& s, bool highlighter) {
  if (highlighter) return Pen::Highlighter;
  if (s == "ballpoint") return Pen::Ballpoint;
  if (s == "pencil") return Pen::Pencil;
  if (s == "calligraphy") return Pen::Calligraphy;
  if (s == "brush") return Pen::Brush;
  return Pen::Fountain;  // also strokes saved before pen styles existed
}

QString penName(Pen pen) {
  switch (pen) {
    case Pen::Ballpoint: return "ballpoint";
    case Pen::Pencil: return "pencil";
    case Pen::Calligraphy: return "calligraphy";
    case Pen::Brush: return "brush";
    case Pen::Highlighter: return "highlighter";
    default: return "fountain";
  }
}

bool speedSensitive(Pen pen) { return pen == Pen::Fountain || pen == Pen::Brush; }

void smoothPoints(const std::vector<Point>& points, std::vector<Point>& into, float streamline) {
  // How far each point moves toward the pen (1 = all the way: no smoothing).
  const float t = 0.15f + (1.f - streamline) * 0.85f;
  for (size_t i = into.size(); i < points.size(); ++i) {
    const Point& r = points[i];
    if (i == 0) {
      into.push_back(r);
      continue;
    }
    const Point& p = into[i - 1];
    into.push_back({p.x + (r.x - p.x) * t, p.y + (r.y - p.y) * t, r.p});
  }
}

namespace {
/// Speed (page units/ms) → pressure-like value; ~0.4 units/ms is ordinary writing.
float speedLevel(float v) { return 0.3f + 0.5f / (1.f + std::pow(v / 0.38f, 1.5f)); }
}  // namespace

float SpeedInk::next(float x, float y, double tMs, float raw) {
  if (m_has) {
    const double dt = std::max(1.0, tMs - m_t);
    const float v = float(std::hypot(x - m_x, y - m_y) / dt);
    m_level += (speedLevel(v) - m_level) * float(std::min(1.0, 0.08 * std::sqrt(dt)));
  }
  m_has = true;
  m_x = x;
  m_y = y;
  m_t = tMs;
  if (raw < 0) return m_level;
  // Stylus: real pressure leads, speed adjusts it by up to about ±25%.
  return std::clamp(raw * (0.75f + 0.6f * (m_level - 0.55f)), 0.02f, 1.f);
}

float segmentDistance(QPointF p, QPointF a, QPointF b) {
  const double dx = b.x() - a.x(), dy = b.y() - a.y();
  const double len = dx * dx + dy * dy;
  double t = len > 0 ? ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len : 0;
  t = std::clamp(t, 0.0, 1.0);
  return float(std::hypot(p.x() - (a.x() + t * dx), p.y() - (a.y() + t * dy)));
}

namespace {

/// perfect-freehand's per-style settings (size = diameter at medium pressure).
struct Nib {
  float size, thinning, taperStart, taperEnd;
};

Nib nibFor(Pen pen, float width) {
  switch (pen) {
    case Pen::Ballpoint: return {std::max(0.8f, width * 0.8f), 0.f, 0, 0};
    case Pen::Pencil: return {std::max(0.8f, width * 0.78f), 0.15f, 0, 0};
    case Pen::Brush: return {width * 1.25f, 0.82f, width * 3, width * 4};
    case Pen::Highlighter: return {width, 0.f, 0, 0};
    case Pen::Calligraphy: return {width * 0.9f, 0.3f, 0, 0};
    default: return {width * 0.875f, 0.5f, 0, 0};
  }
}

/// perfect-freehand's radius for a pressure (sine easing out of the thinning).
float radiusFor(const Nib& nib, float pressure) {
  const float t = 0.5f - nib.thinning * (0.5f - pressure);
  return std::max(0.25f, nib.size * t);
}

void addTri(Mesh& m, QPointF a, QPointF b, QPointF c) {
  m.tris.push_back(a);
  m.tris.push_back(b);
  m.tris.push_back(c);
}

/// A filled circle (round caps, joins and dots).
void addDisc(Mesh& m, QPointF c, float r) {
  const int n = std::clamp(int(r * 2.5f) + 10, 12, 40);
  QPointF prev(c.x() + r, c.y());
  for (int i = 1; i <= n; ++i) {
    const double a = 2 * M_PI * i / n;
    const QPointF next(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
    addTri(m, c, prev, next);
    prev = next;
  }
}

}  // namespace

void strokeMesh(const std::vector<Point>& raw, Pen pen, float width, float streamline, Mesh& out) {
  out.tris.clear();
  if (raw.empty()) return;
  const Nib nib = nibFor(pen, width);
  // A tap: a round dot as wide as the line (whatever the pressure).
  float x0 = raw[0].x, x1 = raw[0].x, y0 = raw[0].y, y1 = raw[0].y;
  for (const Point& p : raw) {
    x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
  }
  if (std::max(x1 - x0, y1 - y0) <= width * 0.6f) {
    const float k = pen == Pen::Brush ? 0.6f : (pen == Pen::Ballpoint || pen == Pen::Pencil) ? 0.42f : 0.5f;
    addDisc(out, QPointF((x0 + x1) / 2, (y0 + y1) / 2), std::max(0.6f, width * k));
    return;
  }
  std::vector<Point> pts;
  smoothPoints(raw, pts, pen == Pen::Highlighter ? 0.3f : streamline);
  pts.back() = raw.back();
  // Drop points too close together (they make jagged normals).
  std::vector<Point> p;
  p.reserve(pts.size());
  for (const Point& q : pts) {
    if (!p.empty() && std::hypot(q.x - p.back().x, q.y - p.back().y) < 0.15f) continue;
    p.push_back(q);
  }
  if (p.size() < 2) {
    addDisc(out, QPointF(p[0].x, p[0].y), radiusFor(nib, p[0].p));
    return;
  }
  // Radii, with smoothed pressure and tapers.
  const size_t n = p.size();
  std::vector<float> len(n, 0.f);
  for (size_t i = 1; i < n; ++i) len[i] = len[i - 1] + std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
  const float total = len.back();
  std::vector<float> r(n);
  float pr = p[0].p;
  for (size_t i = 0; i < n; ++i) {
    pr += (p[i].p - pr) * 0.35f;
    float rad = pen == Pen::Highlighter ? nib.size / 2 : radiusFor(nib, nib.thinning > 0 ? pr : 0.5f);
    if (nib.taperStart > 0 && len[i] < nib.taperStart) rad *= std::max(0.15f, len[i] / nib.taperStart);
    if (nib.taperEnd > 0 && total - len[i] < nib.taperEnd) rad *= std::max(0.15f, (total - len[i]) / nib.taperEnd);
    r[i] = rad;
  }
  // Direction of each segment and the normal at each point (averaged).
  auto dir = [&](size_t i) {  // segment i → i+1
    const double dx = p[i + 1].x - p[i].x, dy = p[i + 1].y - p[i].y;
    const double l = std::max(1e-6, std::hypot(dx, dy));
    return QPointF(dx / l, dy / l);
  };
  QPointF prevL, prevR;
  for (size_t i = 0; i < n; ++i) {
    QPointF d;
    bool sharp = false;
    if (i == 0) d = dir(0);
    else if (i == n - 1) d = dir(n - 2);
    else {
      const QPointF a = dir(i - 1), b = dir(i);
      const double dot = a.x() * b.x() + a.y() * b.y();
      sharp = dot < 0.8;  // a turn of more than ~37°: a round join instead of a fold
      d = QPointF(a.x() + b.x(), a.y() + b.y());
      const double l = std::hypot(d.x(), d.y());
      d = l < 1e-6 ? b : QPointF(d.x() / l, d.y() / l);
    }
    const QPointF nrm(-d.y(), d.x());
    const QPointF c(p[i].x, p[i].y);
    const QPointF L = c + nrm * r[i], R = c - nrm * r[i];
    if (i > 0) {
      addTri(out, prevL, prevR, L);
      addTri(out, prevR, R, L);
    }
    if (sharp) {
      addDisc(out, c, r[i]);
      // Restart the strip along the outgoing direction.
      const QPointF b = dir(i), n2(-b.y(), b.x());
      prevL = c + n2 * r[i];
      prevR = c - n2 * r[i];
    } else {
      prevL = L;
      prevR = R;
    }
  }
  // Round ends (the highlighter has flat ends).
  if (pen != Pen::Highlighter) {
    addDisc(out, QPointF(p[0].x, p[0].y), r[0]);
    addDisc(out, QPointF(p[n - 1].x, p[n - 1].y), r[n - 1]);
  }
}

}  // namespace ink
