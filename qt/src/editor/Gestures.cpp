#include "Gestures.h"

#include <algorithm>
#include <cmath>

#include "Geometry.h"

namespace ed {

namespace {

// --- what counts as a scribble (see Gestures.h; tested in GestureTests) ---
/// Back-and-forth legs it needs (5: across, back, across, back, across).
constexpr int kMinLegs = 5;
/// A leg goes most of the way across: a turn counts once the pen has come
/// back this share of the scribble's span.
constexpr double kTurnShare = 0.35;
/// Most legs span at least this share of the whole width (not a jitter).
constexpr double kLegShare = 0.5;
/// Sideways travel per leg, against its length: small for a scribble
/// (it comes back over the same place), about 1 for a loop or a circle.
constexpr double kMaxSideways = 0.5;
/// It has some height (a line gone over again to thicken it has none).
constexpr double kMinHeightShare = 0.12;
/// Ink under it: this share of the element's points inside its area.
constexpr double kCovered = 0.75;

std::vector<P2> convexHull(std::vector<P2> p) {
  std::sort(p.begin(), p.end());
  p.erase(std::unique(p.begin(), p.end()), p.end());
  if (p.size() < 3) return p;
  auto cross = [](const P2& o, const P2& a, const P2& b) { return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); };
  std::vector<P2> h(2 * p.size());
  size_t k = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
    h[k++] = p[i];
  }
  for (size_t i = p.size() - 1, t = k + 1; i > 0; --i) {
    while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 0) --k;
    h[k++] = p[i - 1];
  }
  h.resize(k - 1);
  return h;
}

/// Points along an element, for asking how much of it is covered.
std::vector<P2> samples(const El& el) {
  std::vector<P2> out;
  auto take = [&](const std::vector<P2>& pts) {
    const size_t step = std::max<size_t>(1, pts.size() / 64);
    for (size_t i = 0; i < pts.size(); i += step) out.push_back(pts[i]);
    if (!pts.empty()) out.push_back(pts.back());
  };
  if (el.kind == Kind::Stroke) {
    std::vector<P2> pts;
    for (const Pt& p : el.points) pts.push_back({p[0], p[1]});
    take(pts);
  } else if (el.kind == Kind::Shape) {
    for (std::vector<P2> part : shapeParts(el.shape)) {
      // Along each side, not just its corners.
      std::vector<P2> dense;
      for (size_t i = 0; i + 1 < part.size(); ++i)
        for (int k = 0; k < 8; ++k) dense.push_back({part[i][0] + (part[i + 1][0] - part[i][0]) * k / 8.0, part[i][1] + (part[i + 1][1] - part[i][1]) * k / 8.0});
      if (!part.empty()) dense.push_back(part.back());
      take(dense);
    }
    if (el.shape.kind == "ellipse")
      for (int k = 0; k < 24; ++k) out.push_back({el.shape.cx + el.shape.rx * std::cos(k * M_PI / 12), el.shape.cy + el.shape.ry * std::sin(k * M_PI / 12)});
  } else if (el.kind == Kind::Text) {
    const Rect r = bounds(el);
    for (int i = 0; i <= 4; ++i)
      for (int j = 0; j <= 2; ++j) out.push_back({r.x + r.w * (0.05 + 0.9 * i / 4), r.y + r.h * (0.1 + 0.8 * j / 2)});
  }
  return out;
}
}  // namespace

std::optional<Scribble> asScribble(const std::vector<Pt>& points) {
  if (points.size() < 16) return std::nullopt;
  std::vector<P2> pts;
  pts.reserve(points.size());
  for (const Pt& p : points) pts.push_back({p[0], p[1]});
  // Its main direction (the one it goes back and forth along).
  double mx = 0, my = 0;
  for (const P2& p : pts) mx += p[0], my += p[1];
  mx /= pts.size(), my /= pts.size();
  double sxx = 0, syy = 0, sxy = 0;
  for (const P2& p : pts) sxx += (p[0] - mx) * (p[0] - mx), syy += (p[1] - my) * (p[1] - my), sxy += (p[0] - mx) * (p[1] - my);
  const double angle = 0.5 * std::atan2(2 * sxy, sxx - syy);
  const double ux = std::cos(angle), uy = std::sin(angle);
  std::vector<double> along, side;
  for (const P2& p : pts) along.push_back((p[0] - mx) * ux + (p[1] - my) * uy), side.push_back(-(p[0] - mx) * uy + (p[1] - my) * ux);
  const auto [amin, amax] = std::minmax_element(along.begin(), along.end());
  const auto [smin, smax] = std::minmax_element(side.begin(), side.end());
  const double span = *amax - *amin, height = *smax - *smin;
  if (span < 12) return std::nullopt;
  if (height < std::max(4.0, span * kMinHeightShare)) return std::nullopt;

  // Its turns along that direction (with some slack, so a wobble isn't one).
  const double slack = std::max(4.0, span * kTurnShare);
  std::vector<size_t> turns{0};
  int dir = 0;
  size_t extreme = 0;
  for (size_t i = 1; i < along.size(); ++i) {
    if (dir == 0) {
      if (std::abs(along[i] - along[0]) > slack) dir = along[i] > along[0] ? 1 : -1, extreme = i;
    } else if (dir > 0) {
      if (along[i] > along[extreme]) extreme = i;
      else if (along[extreme] - along[i] > slack) turns.push_back(extreme), dir = -1, extreme = i;
    } else {
      if (along[i] < along[extreme]) extreme = i;
      else if (along[i] - along[extreme] > slack) turns.push_back(extreme), dir = 1, extreme = i;
    }
  }
  turns.push_back(along.size() - 1);
  const int legs = int(turns.size()) - 1;
  if (legs < kMinLegs) return std::nullopt;

  // Each leg: most of the way across, and back over the same place (not
  // round in a loop, not moving on like a letter or a zigzag).
  int wide = 0;
  double across = 0, sideways = 0;
  for (size_t k = 0; k + 1 < turns.size(); ++k) {
    const size_t a = turns[k], b = turns[k + 1];
    const double len = std::abs(along[b] - along[a]);
    if (len >= span * kLegShare) ++wide;
    across += len;
    for (size_t i = a + 1; i <= b; ++i) sideways += std::abs(side[i] - side[i - 1]);
  }
  if (wide < legs * 0.7) return std::nullopt;
  if (sideways > across * kMaxSideways) return std::nullopt;

  Scribble s;
  s.hull = convexHull(pts);
  s.box = rectOfPts(pts);
  s.legs = legs;
  return s;
}

QSet<QString> scratchedOut(const Scribble& s, const std::vector<const El*>& els) {
  QSet<QString> out;
  if (s.hull.size() < 3) return out;
  for (const El* el : els) {
    if (el->kind != Kind::Stroke && el->kind != Kind::Shape && el->kind != Kind::Text) continue;
    const Rect b = bounds(*el);
    // Nowhere near, or much bigger than the scribble: not what it covers.
    if (b.x > s.box.x + s.box.w || b.x + b.w < s.box.x || b.y > s.box.y + s.box.h || b.y + b.h < s.box.y) continue;
    if (b.w * b.h > s.box.w * s.box.h * 1.6 + 400) continue;
    const std::vector<P2> pts = samples(*el);
    if (pts.empty()) continue;
    int inside = 0;
    for (const P2& p : pts)
      if (pointInPolygon(p[0], p[1], s.hull)) ++inside;
    if (inside >= pts.size() * kCovered) out.insert(el->id);
  }
  return out;
}

}  // namespace ed
