#include "Instrument.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>

namespace ed {
namespace {

const double RULER_L = 300 * MM, RULER_H = 15 * MM, PROT_R = 75 * MM, PROT_BASE = 9 * MM;
const double SNAP_ANGLES[] = {0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180};
const double SNAP_DEG = 2.5;

double jsRound(double v) { return std::floor(v + 0.5); }

/// Number.toFixed (no "-0.00").
QString fixed(double n, int digits = 2) {
  QString s = QString::number(n, 'f', digits);
  if (s.startsWith('-') && s.mid(1).count(QRegularExpression("[1-9]")) == 0) s = s.mid(1);
  return s;
}
QString f(double n) { return fixed(n, 2); }

struct Out {
  P2 d, n;
  double len;
};
/// Outward unit normal of edge a→b for a shape around the origin.
Out outward(P2 a, P2 b) {
  const double len = std::hypot(b[0] - a[0], b[1] - a[1]);
  const P2 d{(b[0] - a[0]) / len, (b[1] - a[1]) / len};
  const P2 mid{(a[0] + b[0]) / 2, (a[1] + b[1]) / 2};
  P2 n{-d[1], d[0]};
  if (n[0] * mid[0] + n[1] * mid[1] < 0) n = {-n[0], -n[1]};
  return {d, n, len};
}

}  // namespace

double snapAngle(double rad, bool coarse) {
  double deg = (rad * 180) / M_PI;
  deg = std::fmod(std::fmod(deg, 360) + 360, 360);
  if (coarse) return (jsRound(deg / 15) * 15 * M_PI) / 180;
  for (double base : {0.0, 180.0})
    for (double s : SNAP_ANGLES) {
      const double target = base + s;
      if (std::abs(deg - target) < SNAP_DEG) return (std::fmod(target, 360) * M_PI) / 180;
    }
  if (std::abs(deg - 360) < SNAP_DEG) return 0;
  return (jsRound(deg) * M_PI) / 180;
}

P2 projectOnto(const EdgeGuide& g, P2 p, double offset) {
  if (g.arc) {
    const double dx = p[0] - g.c[0], dy = p[1] - g.c[1];
    double d = std::hypot(dx, dy);
    if (d == 0) d = 1;
    return {g.c[0] + (dx / d) * (g.r + offset), g.c[1] + (dy / d) * (g.r + offset)};
  }
  const double t = (p[0] - g.a[0]) * g.dir[0] + (p[1] - g.a[1]) * g.dir[1];
  return {g.a[0] + g.dir[0] * t + g.normal[0] * offset, g.a[1] + g.dir[1] * t + g.normal[1] * offset};
}

QString Instrument::tickMarks(P2 a, P2 b, P2 inward, double from, bool numbers) {
  const double len = std::hypot(b[0] - a[0], b[1] - a[1]);
  const P2 d{(b[0] - a[0]) / len, (b[1] - a[1]) / len};
  QString parts, labels;
  for (int k = 0; from + k * MM <= len - 2 * MM; k++) {
    const double t = from + k * MM;
    const double l = k % 10 == 0 ? 3.2 * MM : k % 5 == 0 ? 2.2 * MM : 1.3 * MM;
    const P2 p{a[0] + d[0] * t, a[1] + d[1] * t};
    parts += QString("M%1 %2l%3 %4").arg(f(p[0]), f(p[1]), f(inward[0] * l), f(inward[1] * l));
    if (numbers && k % 10 == 0) {
      const P2 q{p[0] + inward[0] * 5.4 * MM, p[1] + inward[1] * 5.4 * MM};
      double r = (std::atan2(d[1], d[0]) * 180) / M_PI;
      if (r > 90 || r < -90) r += 180;  // keep numbers upright
      // (Qt's SVG renderer ignores dy in em: 0.35 × the 7px font as an offset.)
      labels += QString("<text transform=\"translate(%1 %2) rotate(%3)\" y=\"2.45\">%4</text>").arg(f(q[0]), f(q[1]), fixed(r, 1)).arg(k / 10);
    }
  }
  return "<path class=\"ticks\" d=\"" + parts + "\"/>" + labels;
}

Instrument::Shape Instrument::polygonShape(const std::vector<P2>& verts, const std::vector<std::pair<int, int>>& markLegs) {
  // The pivot is the centroid.
  P2 g{0, 0};
  for (const P2& v : verts) {
    g[0] += v[0];
    g[1] += v[1];
  }
  g = {g[0] / verts.size(), g[1] / verts.size()};
  std::vector<P2> v;
  for (const P2& p : verts) v.push_back({p[0] - g[0], p[1] - g[1]});
  double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
  for (const P2& p : v) {
    x0 = std::min(x0, p[0]);
    y0 = std::min(y0, p[1]);
    x1 = std::max(x1, p[0]);
    y1 = std::max(y1, p[1]);
  }
  Shape s;
  s.box = {x0, y0, x1 - x0, y1 - y0};
  for (size_t i = 0; i < v.size(); ++i) s.lines.push_back({v[i], v[(i + 1) % v.size()]});
  auto path = [](const std::vector<P2>& pts) {
    QStringList l;
    for (const P2& p : pts) l << f(p[0]) + " " + f(p[1]);
    return "M" + l.join("L") + "Z";
  };
  std::vector<P2> cut;
  for (const P2& p : v) cut.push_back({p[0] * 0.4, p[1] * 0.4});
  // Decorative inner cut-out, like a real set square.
  s.marks = "<path class=\"cutout\" d=\"" + path(cut) + "\"/>";
  for (const auto& [i, j] : markLegs) {
    const Out o = outward(v[size_t(i)], v[size_t(j)]);
    s.marks += tickMarks(v[size_t(i)], v[size_t(j)], {-o.n[0], -o.n[1]}, 4 * MM, true);
  }
  s.outline = path(v);
  // Grips near the two acute corners, pulled towards the middle.
  s.grips = {{v[1][0] * 0.6, v[1][1] * 0.6}, {v[2][0] * 0.6, v[2][1] * 0.6}};
  return s;
}

Instrument::Shape Instrument::makeShape(const QString& kind) {
  if (kind == "protractor") {
    const double R = PROT_R, B = PROT_BASE;
    QString ticks, labels;
    for (int d = 0; d <= 180; d++) {
      const double th = (d * M_PI) / 180;
      const P2 u{std::cos(th), -std::sin(th)};
      const double l = d % 10 == 0 ? 6 * MM : d % 5 == 0 ? 4 * MM : 2.2 * MM;
      ticks += QString("M%1 %2L%3 %4").arg(f(u[0] * R), f(u[1] * R), f(u[0] * (R - l)), f(u[1] * (R - l)));
      if (d % 10 == 0) {
        const int rot = 90 - d;
        const P2 o{u[0] * (R - 9.5 * MM), u[1] * (R - 9.5 * MM)};
        const P2 i{u[0] * (R - 15 * MM), u[1] * (R - 15 * MM)};
        labels += QString("<text transform=\"translate(%1 %2) rotate(%3)\" y=\"2.45\">%4</text>").arg(f(o[0]), f(o[1])).arg(rot).arg(d);
        labels += QString("<text class=\"inner\" transform=\"translate(%1 %2) rotate(%3)\" y=\"1.93\">%4</text>").arg(f(i[0]), f(i[1])).arg(rot).arg(180 - d);
      }
    }
    const double c = 2.5 * MM;
    Shape s;
    s.marks = "<path class=\"ticks\" d=\"" + ticks + "\"/>" + labels +
              QString("<path class=\"ticks\" d=\"M%1 0H%2M%3 0H%4M0 %5V%6\"/>").arg(f(-R + 4 * MM), f(R - 4 * MM), f(-c), f(c), f(-c), f(c));
    s.outline = QString("M%1 %2V0A%3 %4 0 0 1 %5 0V%6Z").arg(f(-R), f(B), f(R), f(R), f(R), f(B));
    s.lines = {{{R, B}, {-R, B}}};
    s.arc = R;
    s.box = {-R, -R, 2 * R, R + B};
    s.grips = {{-R + 6 * MM, B / 2}, {R - 6 * MM, B / 2}};
    s.labelAt = {0, -R * 0.34};
    return s;
  }
  if (kind == "square45") {
    const double S = 120 * MM;
    return polygonShape({{0, 0}, {S, 0}, {0, -S}}, {{0, 1}, {0, 2}});
  }
  if (kind == "square60") {
    const double S = 80 * MM;
    return polygonShape({{0, 0}, {S * std::sqrt(3.0), 0}, {0, -S}}, {{0, 1}, {0, 2}});
  }
  const double L = RULER_L, H = RULER_H;
  const P2 tl{-L / 2, -H / 2}, tr{L / 2, -H / 2}, bl{-L / 2, H / 2}, br{L / 2, H / 2};
  Shape s;
  s.outline = QString("M%1 %2H%3V%4H%5Z").arg(f(-L / 2), f(-H / 2), f(L / 2), f(H / 2), f(-L / 2));
  s.lines = {{tl, tr}, {bl, br}};
  s.box = {-L / 2, -H / 2, L, H};
  s.marks = tickMarks(tl, tr, {0, 1}, 6 * MM, true) + tickMarks(bl, br, {0, -1}, 6 * MM, false);
  s.grips = {{-L / 4, 0}, {L / 4, 0}};
  return s;
}

Instrument::Instrument() : m_shape(makeShape("ruler")) {}

void Instrument::setKind(const QString& k) {
  if (k == kind) return;
  kind = k;
  m_shape = makeShape(k);
}

void Instrument::show(double x, double y) {
  visible = true;
  const Rect& b = m_shape.box;
  // Protractor arms reach a little beyond its body.
  const double top = kind == "protractor" ? b.y - 12 * MM : b.y;
  cx = x - (b.x + b.w / 2);
  cy = y - (top + (b.y + b.h - top) / 2);
  angle = 0;
}

P2 Instrument::toPage(P2 p) const {
  const double c = std::cos(angle), s = std::sin(angle);
  return {cx + p[0] * c - p[1] * s, cy + p[0] * s + p[1] * c};
}
P2 Instrument::toLocal(P2 p) const {
  const double c = std::cos(angle), s = std::sin(angle);
  const double dx = p[0] - cx, dy = p[1] - cy;
  return {dx * c + dy * s, -dx * s + dy * c};
}
P2 Instrument::rot(P2 v) const {
  const double c = std::cos(angle), s = std::sin(angle);
  return {v[0] * c - v[1] * s, v[0] * s + v[1] * c};
}

std::optional<EdgeGuide> Instrument::edgeNear(P2 p, double band) const {
  if (!visible) return std::nullopt;
  const P2 q = toLocal(p);
  for (const Line& l : m_shape.lines) {
    const Out o = outward(l.a, l.b);
    const P2 rel{q[0] - l.a[0], q[1] - l.a[1]};
    const double out = rel[0] * o.n[0] + rel[1] * o.n[1];
    const double t = rel[0] * o.d[0] + rel[1] * o.d[1];
    if (out >= -2 && out <= band && t >= -band && t <= o.len + band) {
      EdgeGuide g;
      g.a = toPage(l.a);
      g.dir = rot(o.d);
      g.normal = rot(o.n);
      return g;
    }
  }
  if (m_shape.arc) {
    const double r = *m_shape.arc;
    const double d = std::hypot(q[0], q[1]);
    if (d >= r - 2 && d <= r + band && q[1] <= band) {
      EdgeGuide g;
      g.arc = true;
      g.c = {cx, cy};
      g.r = r;
      return g;
    }
  }
  return std::nullopt;
}

bool Instrument::contains(double x, double y) const {
  if (!visible) return false;
  const P2 l = toLocal({x, y});
  const Shape& sh = m_shape;
  if (sh.arc) return std::hypot(l[0], l[1]) <= *sh.arc && l[1] <= sh.box.y + sh.box.h;
  std::vector<P2> poly;
  for (const Line& ln : sh.lines) poly.push_back(ln.a);
  bool inside = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const double xi = poly[i][0], yi = poly[i][1], xj = poly[j][0], yj = poly[j][1];
    if ((yi > l[1]) != (yj > l[1]) && l[0] < ((xj - xi) * (l[1] - yi)) / (yj - yi) + xi) inside = !inside;
  }
  return inside || (sh.lines.size() == 2 && l[0] >= sh.box.x && l[0] <= sh.box.x + sh.box.w && l[1] >= sh.box.y && l[1] <= sh.box.y + sh.box.h);
}

P2 Instrument::armEnd(int i) const {
  const double th = (arms[i] * M_PI) / 180;
  const double r = PROT_R + 10 * MM;
  return {std::cos(th) * r, -std::sin(th) * r};
}

QJsonObject Instrument::describe(double zoom, double viewWidth) const {
  if (!visible) return {{"visible", false}, {"kind", kind}};
  const double pad = 14 * MM;
  const Rect& b0 = m_shape.box;
  const Rect box{b0.x - pad, b0.y - pad, b0.w + 2 * pad, b0.h + 2 * pad};
  // The body and markings, styled as the web app's CSS did (Qt's SVG
  // renderer has no rgba(): colours and opacities separately).
  const QString style =
      "<style>.ruler-body{fill:#f8fafc;fill-opacity:0.62;stroke:#0f172a;stroke-opacity:0.35;stroke-width:0.8}.ticks{stroke:#0f172a;stroke-opacity:0.7;stroke-width:0.35;fill:none}text{fill:#0f172a;fill-opacity:0.75;font-family:Inter;font-weight:bold;font-size:7px;text-anchor:middle}text.inner{font-size:5.5px;fill-opacity:0.5}.cutout{fill:none;stroke:#0f172a;stroke-opacity:0.25;stroke-width:0.8}.arm{stroke:#db2777;stroke-width:1.2;fill:none}.arm-arc{stroke:#db2777;stroke-width:1;fill:#db2777;fill-opacity:0.08}</style>";
  QString svg = QString("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%1 %2 %3 %4\" aria-hidden=\"true\">").arg(f(box.x), f(box.y), f(box.w), f(box.h)) + style + "<path class=\"ruler-body\" d=\"" + m_shape.outline + "\"/>" + m_shape.marks;
  QString label;
  QJsonArray knobs;
  if (kind == "protractor") {
    const P2 a = armEnd(0), b = armEnd(1);
    const double rr = 14 * MM;
    const double t0 = (std::min(arms[0], arms[1]) * M_PI) / 180, t1 = (std::max(arms[0], arms[1]) * M_PI) / 180;
    const P2 p0{std::cos(t0) * rr, -std::sin(t0) * rr}, p1{std::cos(t1) * rr, -std::sin(t1) * rr};
    svg += QString("<g><path class=\"arm\" d=\"M0 0L%1 %2M0 0L%3 %4\"/><path class=\"arm-arc\" d=\"M%5 %6A%7 %8 0 0 0 %9 %10\"/></g>").arg(f(a[0]), f(a[1]), f(b[0]), f(b[1]), f(p0[0]), f(p0[1]), f(rr), f(rr), f(p1[0])).arg(f(p1[1]));
    label = QString("%1°").arg(int(jsRound(std::abs(arms[0] - arms[1]))));
    knobs = {QJsonArray{a[0], a[1]}, QJsonArray{b[0], b[1]}};
  } else {
    double deg = std::fmod(std::fmod(jsRound((angle * 180) / M_PI), 180) + 180, 180);
    if (deg > 90) deg = 180 - deg;
    label = QString("%1°").arg(deg);
  }
  svg += "</svg>";
  // The grips (the ruler's stay within reach).
  std::vector<P2> grips = m_shape.grips;
  if (kind == "ruler") {
    const double gg = std::max(40.0, std::min((RULER_L * zoom) / 2 - 24, viewWidth * 0.36)) / zoom;
    grips = {{-gg, 0}, {gg, 0}};
  }
  QJsonArray g;
  for (const P2& p : grips) g.append(QJsonArray{p[0], p[1]});
  return {{"visible", true}, {"kind", kind}, {"cx", cx}, {"cy", cy}, {"angle", angle}, {"box", QJsonObject{{"x", box.x}, {"y", box.y}, {"w", box.w}, {"h", box.h}}}, {"svg", svg},
          {"grips", g}, {"knobs", knobs}, {"labelAt", QJsonArray{m_shape.labelAt[0], m_shape.labelAt[1]}}, {"label", label}};
}

}  // namespace ed
