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
  if (s == "marker") return Pen::Marker;
  if (s == "watercolor") return Pen::Watercolor;
  if (s == "neon") return Pen::Neon;
  if (s == "construction") return Pen::Construction;
  if (s == "tape") return Pen::Tape;
  if (s == "fineliner") return Pen::Fineliner;
  if (s == "inkbrush") return Pen::InkBrush;
  if (s == "chalk") return Pen::Chalk;
  if (s == "crayon") return Pen::Crayon;
  if (s == "airbrush") return Pen::Airbrush;
  if (s == "flat") return Pen::FlatBrush;
  return Pen::Fountain;  // also strokes saved before pen styles existed
}

QString penName(Pen pen) {
  switch (pen) {
    case Pen::Ballpoint: return "ballpoint";
    case Pen::Pencil: return "pencil";
    case Pen::Calligraphy: return "calligraphy";
    case Pen::Brush: return "brush";
    case Pen::Highlighter: return "highlighter";
    case Pen::Marker: return "marker";
    case Pen::Watercolor: return "watercolor";
    case Pen::Neon: return "neon";
    case Pen::Construction: return "construction";
    case Pen::Tape: return "tape";
    case Pen::Fineliner: return "fineliner";
    case Pen::InkBrush: return "inkbrush";
    case Pen::Chalk: return "chalk";
    case Pen::Crayon: return "crayon";
    case Pen::Airbrush: return "airbrush";
    case Pen::FlatBrush: return "flat";
    default: return "fountain";
  }
}

bool speedSensitive(Pen pen) { return pen == Pen::Fountain || pen == Pen::Brush || pen == Pen::Watercolor || pen == Pen::InkBrush; }

bool dashable(const QString& s) { return s == "ballpoint" || s == "fineliner" || s == "marker" || s == "neon" || s == "construction"; }

bool meshDrawn(const QString& style, bool highlighter) {
  return highlighter || style.isEmpty() || style == "fountain" || style == "ballpoint" || style == "brush" || style == "fineliner";
}

float tapeWidth(float penWidth) { return std::clamp(penWidth * 5.f, 12.f, 80.f); }

QColor liveColor(const QColor& c, Pen pen) {
  QColor out = c;
  switch (pen) {
    case Pen::Highlighter: out.setAlphaF(0.35f); break;
    case Pen::Marker: out.setAlphaF(0.62f); break;
    case Pen::Watercolor: out.setAlphaF(0.3f); break;
    case Pen::Chalk: out.setAlphaF(0.78f); break;
    case Pen::Crayon: out.setAlphaF(0.85f); break;
    case Pen::Airbrush: out.setAlphaF(0.22f); break;
    case Pen::FlatBrush: out.setAlphaF(0.8f); break;
    case Pen::InkBrush: out.setAlphaF(0.94f); break;
    case Pen::Neon:  // the bright core
      out = QColor::fromRgbF(c.redF() + (1 - c.redF()) * 0.55f, c.greenF() + (1 - c.greenF()) * 0.55f, c.blueF() + (1 - c.blueF()) * 0.55f);
      break;
    default: break;
  }
  return out;
}

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
    case Pen::Marker: return {width * 2.4f, 0.1f, 0, 0};
    case Pen::Watercolor: return {width * 3.2f, 0.55f, width * 3, width * 4};
    case Pen::Neon: return {std::max(0.8f, width * 0.8f), 0.f, 0, 0};
    case Pen::Construction: return {std::max(0.7f, width * 0.6f), 0.f, 0, 0};
    case Pen::Tape: return {width, 0.f, 0, 0};
    case Pen::Fineliner: return {std::max(0.6f, width * 0.7f), 0.f, 0, 0};
    case Pen::InkBrush: return {width * 2.f, 0.92f, width * 2, width * 6};
    case Pen::Chalk: return {width * 2.f, 0.05f, 0, 0};
    case Pen::Crayon: return {width * 1.7f, 0.15f, 0, 0};
    case Pen::Airbrush: return {width * 1.2f, 0.f, 0, 0};
    case Pen::FlatBrush: return {width * 2.4f, 0.2f, 0, 0};
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

/// Half a disc on the end of a line: `d` points out of the line.
void addCap(Mesh& m, QPointF c, float r, QPointF d) {
  const double a0 = std::atan2(d.y(), d.x()) - M_PI / 2;
  const int n = std::clamp(int(r * 1.5f) + 6, 8, 24);
  QPointF prev(c.x() + r * std::cos(a0), c.y() + r * std::sin(a0));
  for (int i = 1; i <= n; ++i) {
    const double a = a0 + M_PI * i / n;
    const QPointF next(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
    addTri(m, c, prev, next);
    prev = next;
  }
}

/// A straight band from a to b, `r` either side.
void addBand(Mesh& m, QPointF a, QPointF b, float r) {
  const double dx = b.x() - a.x(), dy = b.y() - a.y();
  const double l = std::max(1e-6, std::hypot(dx, dy));
  const QPointF n(-dy / l * r, dx / l * r);
  addTri(m, a + n, a - n, b + n);
  addTri(m, a - n, b - n, b + n);
}

/// Construction lines: dashes or dots along the polyline.
void dashMesh(const std::vector<Point>& p, float r, bool dots, float gap, Mesh& out) {
  const float on = dots ? 0.f : r * 8.f, off = (dots ? r * 4.5f : r * 5.f) * gap;
  float pos = 0;  // along the current dash period
  bool inDash = true;
  for (size_t i = 0; i + 1 < p.size(); ++i) {
    QPointF a(p[i].x, p[i].y);
    const QPointF b(p[i + 1].x, p[i + 1].y);
    float segLen = float(std::hypot(b.x() - a.x(), b.y() - a.y()));
    while (segLen > 1e-4f) {
      const float left = (inDash ? on : off) - pos;
      const float step = std::min(left, segLen);
      const QPointF next = a + (b - a) * (step / segLen);
      if (inDash && !dots && step > 0) addBand(out, a, next, r);
      a = next;
      segLen -= step;
      pos += step;
      if (pos >= (inDash ? on : off) - 1e-4f) {
        if (inDash && dots) addDisc(out, a, r);
        else if (inDash) addDisc(out, a, r);  // a round end
        inDash = !inDash;
        pos = 0;
        if (inDash && !dots) addDisc(out, a, r);  // and a round start
      }
    }
  }
  if (inDash && !dots && pos > 0) addDisc(out, QPointF(p.back().x, p.back().y), r);
}

}  // namespace

void strokeMesh(const std::vector<Point>& raw, Pen pen, float width, float streamline, Mesh& out, int dashed, float gap, float minGap) {
  out.tris.clear();
  out.glow.clear();
  if (raw.empty()) return;
  const Nib nib = nibFor(pen, width);
  // Tape: a straight strip from where it started to where it is.
  if (pen == Pen::Tape) {
    const QPointF a(raw.front().x, raw.front().y), b(raw.back().x, raw.back().y);
    if (std::hypot(b.x() - a.x(), b.y() - a.y()) < 1) addBand(out, a - QPointF(width / 2, 0), a + QPointF(width / 2, 0), width / 2);
    else addBand(out, a, b, width / 2);
    return;
  }
  // See-through pens: nothing drawn twice (overlaps would show darker).
  const bool clear = pen == Pen::Marker || pen == Pen::Watercolor || pen == Pen::Neon || pen == Pen::Chalk || pen == Pen::Crayon || pen == Pen::Airbrush || pen == Pen::FlatBrush;
  // A tap: a round dot as wide as the line (whatever the pressure).
  float x0 = raw[0].x, x1 = raw[0].x, y0 = raw[0].y, y1 = raw[0].y;
  for (const Point& p : raw) {
    x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
  }
  if (std::max(x1 - x0, y1 - y0) <= width * 0.6f) {
    const float k = pen == Pen::Marker ? 1.1f : pen == Pen::Watercolor ? 1.5f : pen == Pen::Brush ? 0.6f : (pen == Pen::Ballpoint || pen == Pen::Pencil || pen == Pen::Neon || pen == Pen::Construction) ? 0.42f : 0.5f;
    const QPointF c((x0 + x1) / 2, (y0 + y1) / 2);
    addDisc(out, c, std::max(0.6f, width * k));
    if (pen == Pen::Neon) {
      Mesh g;
      addDisc(g, c, std::max(0.6f, width * k) * 3.f);
      out.glow = std::move(g.tris);
    }
    return;
  }
  std::vector<Point> pts;
  smoothPoints(raw, pts, pen == Pen::Highlighter ? 0.3f : streamline);
  pts.back() = raw.back();
  // Drop points too close together (they make jagged normals).
  std::vector<Point> p;
  p.reserve(pts.size());
  for (const Point& q : pts) {
    if (!p.empty() && std::hypot(q.x - p.back().x, q.y - p.back().y) < minGap) continue;
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
  if (dashed > 0) {
    dashMesh(p, dashed == 2 ? std::max(0.55f, width * 0.45f) : r[0], dashed == 2, gap, out);
    return;
  }
  // Direction of each segment and the normal at each point (averaged).
  auto dir = [&](size_t i) {  // segment i → i+1
    const double dx = p[i + 1].x - p[i].x, dy = p[i + 1].y - p[i].y;
    const double l = std::max(1e-6, std::hypot(dx, dy));
    return QPointF(dx / l, dy / l);
  };
  // The strip; `k` scales the radii. Joins at sharp turns and the ends are
  // round: whole discs for solid ink, half discs (no overlap) for see-through.
  auto strip = [&](Mesh& m, float k) {
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
      const float ri = r[i] * k;
      const QPointF nrm(-d.y(), d.x());
      const QPointF c(p[i].x, p[i].y);
      const QPointF L = c + nrm * ri, R = c - nrm * ri;
      if (i > 0) {
        addTri(m, prevL, prevR, L);
        addTri(m, prevR, R, L);
      }
      if (sharp) {
        if (!clear) addDisc(m, c, ri);
        // Restart the strip along the outgoing direction.
        const QPointF b = dir(i), n2(-b.y(), b.x());
        prevL = c + n2 * ri;
        prevR = c - n2 * ri;
        if (clear) {
          // Fill the wedge on the outside of the turn.
          const QPointF a = dir(i - 1);
          const double cross = a.x() * b.y() - a.y() * b.x();
          const QPointF o1 = cross > 0 ? R : L, o2 = cross > 0 ? prevR : prevL;
          addTri(m, c, o1, o2);
        }
      } else {
        prevL = L;
        prevR = R;
      }
    }
    if (pen == Pen::Highlighter) return;  // flat ends
    const QPointF a(p[0].x, p[0].y), b(p[n - 1].x, p[n - 1].y);
    if (clear) {
      const QPointF d0 = dir(0), d1 = dir(n - 2);
      addCap(m, a, r[0] * k, QPointF(-d0.x(), -d0.y()));
      addCap(m, b, r[n - 1] * k, d1);
    } else {
      addDisc(m, a, r[0] * k);
      addDisc(m, b, r[n - 1] * k);
    }
  };
  strip(out, 1.f);
  if (pen == Pen::Neon || pen == Pen::Airbrush) {
    Mesh g;
    strip(g, 3.f);
    out.glow = std::move(g.tris);
  }
}

float lineWidth(Pen pen, float width) { return pen == Pen::Tape ? tapeWidth(width) : nibFor(pen, width).size; }

}  // namespace ink
