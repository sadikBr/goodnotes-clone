#include "Covers.h"

#include <QRegularExpression>
#include <array>
#include <cmath>
#include <cstdint>

#include "../JsCanvas.h"
#include "Fonts.h"

namespace draw {
namespace {

double jsRound(double v) { return std::floor(v + 0.5); }

/// A small seeded random generator (same cover, same pattern).
class Rng {
public:
  explicit Rng(uint32_t seed) : m_s(seed ? seed : 1) {}
  double operator()() {
    m_s = m_s * 1664525u + 1013904223u;
    return m_s / 4294967296.0;
  }

private:
  uint32_t m_s;
};

using Seg = std::array<double, 4>;

void roundRect(JsCanvas& c, double x, double y, double w, double h, double r) {
  c.moveTo(x + r, y);
  c.lineTo(x + w - r, y);
  c.quadraticCurveTo(x + w, y, x + w, y + r);
  c.lineTo(x + w, y + h - r);
  c.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  c.lineTo(x + r, y + h);
  c.quadraticCurveTo(x, y + h, x, y + h - r);
  c.lineTo(x, y + r);
  c.quadraticCurveTo(x, y, x + r, y);
  c.closePath();
}

void fillRound(JsCanvas& c, const Box& b, double r, const QString& style) {
  c.setFill(style);
  c.beginPath();
  roundRect(c, b.x, b.y, b.w, b.h, r);
  c.fill();
}

/// A soft drop shadow under a rounded box (a few faint layers).
void softShadow(JsCanvas& c, const Box& b, double r, double dy = 4) {
  for (int k = 3; k >= 1; k--) fillRound(c, {b.x - k + 1, b.y - k + dy, b.w + 2 * k - 2, b.h + 2 * k}, r + k, "rgba(0, 0, 0, 0.07)");
}

/// Many parallel lines as one stroke.
void lines(JsCanvas& c, const std::vector<Seg>& segs, const QString& style, double width, std::initializer_list<qreal> dash = {}) {
  if (segs.empty()) return;
  c.save();
  c.setStroke(style);
  c.setLineWidth(width);
  c.setDash(dash);
  c.beginPath();
  for (const Seg& s : segs) {
    c.moveTo(s[0], s[1]);
    c.lineTo(s[2], s[3]);
  }
  c.stroke();
  c.restore();
}

QString alphaCss(const char* rgb, double a) { return QString("rgba(%1, %2)").arg(rgb, jsNumber(a)); }

/// Fine cross threads (cloth texture).
void linen(JsCanvas& c, double w, double h, double alpha) {
  std::vector<Seg> light, dark;
  int i = 0;
  for (double y = 1.5; y < h; y += 2.6, i++) (i % 2 ? light : dark).push_back({0, y, w, y});
  i = 0;
  for (double x = 1.5; x < w; x += 2.6, i++) (i % 2 ? light : dark).push_back({x, 0, x, h});
  lines(c, light, alphaCss("255, 255, 255", alpha), 0.8);
  lines(c, dark, alphaCss("0, 0, 0", alpha), 0.8);
}

/// A soft sheen from the top-left (many faint layers).
void sheen(JsCanvas& c, double w, double h, double strength = 0.0065) {
  c.save();
  c.setFill(alphaCss("255, 255, 255", strength));
  const int n = 28;
  for (int k = 0; k < n; k++) {
    const double t = 1 - double(k) / n;
    c.beginPath();
    c.moveTo(0, 0);
    c.lineTo(w * 1.05 * t, 0);
    c.lineTo(0, h * 0.8 * t);
    c.closePath();
    c.fill();
  }
  c.restore();
}

struct PlateText {
  Box title, date;
  double divider;
};
/// A title plate: box for the text, and the divider line under the title.
PlateText plateText(const Box& p, double pad = 26) {
  const double divider = p.y + p.h * 0.66;
  return {{p.x + pad, p.y + 26, p.w - 2 * pad, divider - p.y - 34}, {p.x + pad, divider + 12, p.w - 2 * pad, 0}, divider};
}

void divider(JsCanvas& c, double x0, double x1, double y, const QString& color, const QString& diamond) {
  const double cx = (x0 + x1) / 2;
  lines(c, {{x0, y, cx - 9, y}, {cx + 9, y, x1, y}}, color, 1);
  c.setFill(diamond);
  c.beginPath();
  c.moveTo(cx, y - 4);
  c.lineTo(cx + 4, y);
  c.lineTo(cx, y + 4);
  c.lineTo(cx - 4, y);
  c.closePath();
  c.fill();
}

// --- layouts ---------------------------------------------------------------------

struct ClassicLayout {
  double spine, bandX, bandW;
  Box plate;
  PlateText text;
};
ClassicLayout classicLayout(double w, double h) {
  const double spine = jsRound(std::max(30.0, w * 0.075));
  const double bandW = std::max(12.0, w * 0.018);
  const double bandX = w - std::max(34.0, w * 0.07) - bandW;
  const double left = spine + 26, right = bandX - 26;
  const double pw = std::min(520.0, (right - left) * 0.84);
  const double ph = std::max(150.0, std::min(220.0, h * 0.17));
  const Box plate{left + (right - left - pw) / 2, jsRound(h * (w > h ? 0.26 : 0.22)), pw, ph};
  return {spine, bandX, bandW, plate, plateText(plate)};
}

Box marblePlate(double w, double h) {
  const double pw = std::min(460.0, w * 0.6);
  const double ph = std::max(170.0, std::min(230.0, h * 0.19));
  return {(w + std::max(40.0, w * 0.09) - pw) / 2, h * 0.17, pw, ph};
}

Box kraftLabel(double w, double h) {
  const double band = std::max(36.0, w * 0.09);
  const double pw = std::min(440.0, (w - band) * 0.66);
  const double ph = std::max(150.0, std::min(200.0, h * 0.16));
  return {band + (w - band - pw) / 2, h * 0.2, pw, ph};
}

/// The cream panel in the middle of the zellige cover.
Box zelligePanel(double w, double h) {
  const double pw = std::min(w * 0.76, 620.0);
  const double ph = std::min(h * 0.64, 760.0);
  return {(w - pw) / 2, (h - ph) / 2, pw, ph};
}

struct Circle {
  double x, y, r;
};
Circle dotsLabel(double w, double h) { return {w / 2, h * (w > h ? 0.45 : 0.36), std::min(w, h) * 0.28}; }

/// Collects many small circles to fill in one go (one path, one fill).
class Circles {
public:
  void circle(double x, double y, double r) { m_c.push_back({x, y, r}); }
  void fill(JsCanvas& c, const QString& style) {
    c.setFill(style);
    c.beginPath();
    for (const Circle& k : m_c) {
      c.moveTo(k.x + k.r, k.y);
      c.arc(k.x, k.y, k.r, 0, M_PI * 2);
    }
    c.fill();
  }

private:
  std::vector<Circle> m_c;
};

// --- designs -----------------------------------------------------------------------

void drawClassic(JsCanvas& c, double w, double h, const QString& color) {
  const ClassicLayout L = classicLayout(w, h);
  linen(c, w, h, 0.025);
  sheen(c, w, h);
  c.setFill(shade(color, -0.28));
  c.fillRect(0, 0, L.spine, h);
  c.setFill("rgba(0, 0, 0, 0.18)");
  c.fillRect(L.spine, 0, 4, h);
  c.setFill("rgba(255, 255, 255, 0.10)");
  c.fillRect(L.spine - 3, 0, 2, h);
  const double inset = 16;
  const double sx = L.spine + inset;
  const double sw = w - L.spine - 2 * inset;
  lines(c, {{L.spine - 11, 0, L.spine - 11, h}, {sx, inset, sx + sw, inset}, {sx + sw, inset, sx + sw, h - inset}, {sx + sw, h - inset, sx, h - inset}, {sx, h - inset, sx, inset}},
        "rgba(255, 255, 255, 0.42)", 1.2, {5, 4});
  c.setFill("rgba(0, 0, 0, 0.18)");
  c.fillRect(L.bandX + 3, 0, L.bandW, h);
  c.setFill(shade(color, -0.42));
  c.fillRect(L.bandX, 0, L.bandW, h);
  c.setFill("rgba(255, 255, 255, 0.14)");
  c.fillRect(L.bandX + 2, 0, 1.5, h);
  const Box& p = L.plate;
  softShadow(c, p, 12);
  fillRound(c, p, 12, "#fbf8f1");
  c.setStroke(shade(color, 0.35));
  c.setLineWidth(1.4);
  c.beginPath();
  roundRect(c, p.x + 8, p.y + 8, p.w - 16, p.h - 16, 7);
  c.stroke();
  divider(c, p.x + 40, p.x + p.w - 40, L.text.divider, shade(color, 0.2), shade(color, 0.1));
}

void drawMinimal(JsCanvas& c, double w, double h) {
  sheen(c, w, h, 0.004);
  const double m = std::max(28.0, w * 0.035);
  c.save();
  c.setStroke("rgba(255, 255, 255, 0.45)");
  c.setLineWidth(1);
  c.strokeRect(m, m, w - 2 * m, h - 2 * m);
  c.restore();
  const CoverText t = coverText("minimal", w, h);
  // A short bold bar above the title, and a small mark in the corner.
  c.setFill("#ffffff");
  c.fillRect(t.title.x, t.title.y - 26, 54, 6);
  c.setFill("rgba(255, 255, 255, 0.5)");
  c.beginPath();
  c.arc(w - m - 30, m + 30, 7, 0, M_PI * 2);
  c.fill();
}

void drawGeometric(JsCanvas& c, double w, double h, const QString& color) {
  // Low-poly facets over the top 58%, fading out towards the title.
  Rng rand(7);
  const int cols = 7;
  const double size = w / cols;
  const int rows = int(std::ceil((h * 0.58) / size));
  static const double tones[] = {-0.3, -0.18, -0.08, 0.06, 0.16, 0.26};
  for (int r = 0; r < rows; r++) {
    for (int col = 0; col < cols; col++) {
      const double x = col * size, y = r * size;
      const bool flip = (r + col) % 2 == 0;
      const std::array<std::array<double, 6>, 2> tris = flip ? std::array<std::array<double, 6>, 2>{{{x, y, x + size, y, x, y + size}, {x + size, y, x + size, y + size, x, y + size}}}
                                                             : std::array<std::array<double, 6>, 2>{{{x, y, x + size, y, x + size, y + size}, {x, y, x + size, y + size, x, y + size}}};
      for (const auto& tri : tris) {
        // Fade into the plain colour near the bottom of the pattern.
        const double fade = std::min(1.0, (rows - r) / 2.2);
        const double tone = tones[int(std::floor(rand() * 6))] * fade;
        const QString fill = shade(color, tone);
        c.setFill(fill);
        c.beginPath();
        c.moveTo(tri[0], tri[1]);
        c.lineTo(tri[2], tri[3]);
        c.lineTo(tri[4], tri[5]);
        c.closePath();
        c.fill();
        // Hairline seams hide anti-aliasing gaps between facets.
        c.setStroke(fill);
        c.setLineWidth(0.6);
        c.stroke();
      }
    }
  }
  const CoverText t = coverText("geometric", w, h);
  c.setFill("rgba(255, 255, 255, 0.9)");
  c.fillRect(t.title.x, t.title.y - 24, 46, 5);
}

void drawWaves(JsCanvas& c, double w, double h, const QString& color) {
  sheen(c, w, h, 0.005);
  // Layered waves rising from the bottom, each a little darker.
  struct Layer {
    double top, amp, phase, tone;
  };
  static const Layer layers[] = {{0.5, 0.035, 0.2, 0.18}, {0.6, 0.04, 1.4, 0.05}, {0.7, 0.035, 2.6, -0.12}, {0.8, 0.03, 3.9, -0.26}, {0.89, 0.025, 5.1, -0.4}};
  for (const Layer& l : layers) {
    const double y0 = h * l.top, a = h * l.amp;
    c.setFill(shade(color, l.tone));
    c.beginPath();
    c.moveTo(0, h);
    c.lineTo(0, y0 + std::sin(l.phase) * a);
    const int n = 6;
    for (int k = 1; k <= n; k++) {
      const double x1 = (w * (k - 0.5)) / n, x2 = (w * k) / n;
      c.quadraticCurveTo(x1, y0 + std::sin(l.phase + k * 1.3 - 0.65) * a * 1.8, x2, y0 + std::sin(l.phase + k * 1.3) * a);
    }
    c.lineTo(w, h);
    c.closePath();
    c.fill();
  }
}

void drawMarble(JsCanvas& c, double w, double h, const QString& color) {
  // Composition-book mottle: many small irregular blobs, light and dark.
  Rng rand(11);
  c.setFill(shade(color, -0.2));
  c.fillRect(0, 0, w, h);
  const int count = int(jsRound((w * h) / 520));
  const QString tones[] = {"rgba(255, 255, 255, 0.55)", "rgba(255, 255, 255, 0.3)", "rgba(0, 0, 0, 0.35)", shade(color, 0.25), shade(color, -0.45)};
  for (int i = 0; i < count; i++) {
    const double x = rand() * w;
    const double y = rand() * h;
    const double r1 = rand();
    const double r = 1.5 + r1 * rand() * 9;
    c.setFill(tones[int(std::floor(rand() * 5))]);
    c.beginPath();
    const int pts = 5;
    for (int k = 0; k <= pts; k++) {
      const double a = (double(k) / pts) * M_PI * 2;
      const double rr = r * (0.55 + rand() * 0.6);
      const double px = x + std::cos(a) * rr, py = y + std::sin(a) * rr * 0.8;
      if (k == 0) c.moveTo(px, py);
      else c.lineTo(px, py);
    }
    c.closePath();
    c.fill();
  }
  // Cloth tape spine.
  const double spine = std::max(40.0, w * 0.09);
  c.setFill("#1f2328");
  c.fillRect(0, 0, spine, h);
  linen(c, spine, h, 0.06);
  c.setFill("rgba(255, 255, 255, 0.08)");
  c.fillRect(spine - 3, 0, 2, h);
  // White label with rules.
  const Box p = marblePlate(w, h);
  softShadow(c, p, 8, 3);
  fillRound(c, p, 8, "#fbfaf7");
  c.setStroke("#1f2328");
  c.setLineWidth(2);
  c.beginPath();
  roundRect(c, p.x + 7, p.y + 7, p.w - 14, p.h - 14, 5);
  c.stroke();
  lines(c, {{p.x + 24, p.y + p.h * 0.6, p.x + p.w - 24, p.y + p.h * 0.6}, {p.x + 24, p.y + p.h * 0.82, p.x + p.w - 24, p.y + p.h * 0.82}}, "rgba(31, 35, 40, 0.35)", 1);
}

void drawLeather(JsCanvas& c, double w, double h, const QString& color) {
  c.setFill(shade(color, -0.42));
  c.fillRect(0, 0, w, h);
  // Grain: tiny pores, light and dark.
  Rng rand(23);
  const int pores = int(jsRound((w * h) / 110));
  Circles dark, light;
  for (int i = 0; i < pores; i++) {
    const double x = rand() * w;
    const double y = rand() * h;
    const double r = 0.5 + rand() * 1.1;
    (rand() < 0.6 ? dark : light).circle(x, y, r);
  }
  dark.fill(c, "rgba(0, 0, 0, 0.22)");
  light.fill(c, "rgba(255, 255, 255, 0.05)");
  sheen(c, w, h, 0.004);
  // Embossed double border.
  const double m = std::max(30.0, w * 0.045);
  c.save();
  c.setLineWidth(1.6);
  const std::pair<double, const char*> borders[] = {{1.2, "rgba(255, 255, 255, 0.08)"}, {0, "rgba(0, 0, 0, 0.45)"}};
  for (const auto& [off, style] : borders) {
    c.setStroke(style);
    c.strokeRect(m + off, m + off, w - 2 * m, h - 2 * m);
    c.strokeRect(m + 8 + off, m + 8 + off, w - 2 * m - 16, h - 2 * m - 16);
  }
  c.restore();
  // Metal corner protectors.
  const double cs = std::max(38.0, w * 0.07);
  const std::array<double, 4> corners[] = {{0, 0, 1, 1}, {w, 0, -1, 1}, {0, h, 1, -1}, {w, h, -1, -1}};
  for (const auto& [x, y, sx, sy] : corners) {
    c.setFill("#b9975b");
    c.beginPath();
    c.moveTo(x, y);
    c.lineTo(x + sx * cs, y);
    c.lineTo(x, y + sy * cs);
    c.closePath();
    c.fill();
    c.setStroke("rgba(255, 255, 255, 0.35)");
    c.setLineWidth(1);
    c.beginPath();
    c.moveTo(x + sx * (cs - 6), y + sy * 2);
    c.lineTo(x + sx * 2, y + sy * (cs - 6));
    c.stroke();
  }
  // Gold rules around the title.
  const CoverText t = coverText("leather", w, h);
  const double cx = w / 2;
  lines(c, {{cx - 90, t.title.y - 22, cx + 90, t.title.y - 22}, {cx - 90, t.date.y - 14, cx + 90, t.date.y - 14}}, "rgba(232, 207, 138, 0.7)", 1.2);
}

void drawKraft(JsCanvas& c, double w, double h, const QString& color) {
  c.setFill("#c8a77c");
  c.fillRect(0, 0, w, h);
  // Paper fibres.
  Rng rand(31);
  std::vector<Seg> dark, light;
  const int fibres = int(jsRound((w * h) / 260));
  for (int i = 0; i < fibres; i++) {
    const double x = rand() * w;
    const double y = rand() * h;
    const double a = rand() * M_PI;
    const double l = 2 + rand() * 7;
    (rand() < 0.55 ? dark : light).push_back({x, y, x + std::cos(a) * l, y + std::sin(a) * l});
  }
  lines(c, dark, "rgba(92, 62, 30, 0.22)", 0.7);
  lines(c, light, "rgba(255, 244, 222, 0.28)", 0.7);
  // Cloth tape in the notebook colour.
  const double band = std::max(36.0, w * 0.09);
  c.setFill(color);
  c.fillRect(0, 0, band, h);
  linen(c, band, h, 0.07);
  c.setFill("rgba(0, 0, 0, 0.15)");
  c.fillRect(band, 0, 3, h);
  // Sticker label with a coloured edge.
  const Box p = kraftLabel(w, h);
  softShadow(c, p, 14, 3);
  fillRound(c, p, 14, "#fffdf8");
  c.setStroke(color);
  c.setLineWidth(3);
  c.beginPath();
  roundRect(c, p.x + 7, p.y + 7, p.w - 14, p.h - 14, 9);
  c.stroke();
  lines(c, {{p.x + 34, p.y + p.h * 0.64, p.x + p.w - 34, p.y + p.h * 0.64}}, "rgba(63, 45, 28, 0.3)", 1, {3, 3});
}

void drawDots(JsCanvas& c, double w, double h, const QString& color) {
  Circles dots;
  const double gap = std::max(30.0, w * 0.042);
  int row = 0;
  for (double y = gap / 2; y < h + gap; y += gap * 0.866, row++)
    for (double x = row % 2 ? gap / 2 : 0; x < w + gap; x += gap) dots.circle(x, y, gap * 0.17);
  dots.fill(c, shade(color, 0.22));
  const Circle l = dotsLabel(w, h);
  // Shadow, white disc, ring in the cover colour.
  for (int k = 3; k >= 1; k--) {
    c.setFill("rgba(0, 0, 0, 0.07)");
    c.beginPath();
    c.arc(l.x, l.y + 4, l.r + k, 0, M_PI * 2);
    c.fill();
  }
  c.setFill("#fffdf8");
  c.beginPath();
  c.arc(l.x, l.y, l.r, 0, M_PI * 2);
  c.fill();
  c.setStroke(shade(color, 0.1));
  c.setLineWidth(2);
  c.beginPath();
  c.arc(l.x, l.y, l.r - 10, 0, M_PI * 2);
  c.stroke();
  const CoverText t = coverText("dots", w, h);
  divider(c, l.x - l.r * 0.45, l.x + l.r * 0.45, t.date.y - 10, shade(color, 0.2), shade(color, 0.05));
}

/// An eight-pointed star (two overlapping squares), as one closed path.
void star8(JsCanvas& c, double cx, double cy, double r, double turn = 0) {
  const double inner = r * 0.7654;  // the two squares' crossing points
  for (int i = 0; i < 16; i++) {
    const double a = turn + (i * M_PI) / 8;
    const double rr = i % 2 ? inner : r;
    const double x = cx + std::cos(a) * rr, y = cy + std::sin(a) * rr;
    if (i == 0) c.moveTo(x, y);
    else c.lineTo(x, y);
  }
  c.closePath();
}

/// Zellige: Moroccan tilework — eight-pointed stars with small diamonds
/// between them, in tints of the cover colour, around a cream panel.
void drawZellige(JsCanvas& c, double w, double h, const QString& color) {
  const double g = std::max(56.0, w * 0.12);
  const double ox = std::fmod(w, g) / 2, oy = std::fmod(h, g) / 2;
  c.beginPath();
  for (double y = oy; y <= h + g; y += g)
    for (double x = ox; x <= w + g; x += g) star8(c, x, y, g * 0.4, M_PI / 8);
  c.setFill(shade(color, 0.18));
  c.fill();
  c.beginPath();
  for (double y = oy; y <= h + g; y += g)
    for (double x = ox; x <= w + g; x += g) star8(c, x, y, g * 0.22, 0);
  c.setFill(shade(color, -0.12));
  c.fill();
  c.beginPath();
  for (double y = oy + g / 2; y <= h + g; y += g)
    for (double x = ox + g / 2; x <= w + g; x += g) {
      const double d = g * 0.13;
      c.moveTo(x, y - d);
      c.lineTo(x + d, y);
      c.lineTo(x, y + d);
      c.lineTo(x - d, y);
      c.closePath();
    }
  c.setFill(shade(color, 0.42));
  c.fill();
  // The panel: soft drop, cream face, double border in the cover colour.
  const Box p = zelligePanel(w, h);
  c.setFill("rgba(0, 0, 0, 0.16)");
  c.fillRect(p.x + 3, p.y + 5, p.w, p.h);
  c.setFill("#fffaf0");
  c.fillRect(p.x, p.y, p.w, p.h);
  c.setStroke(shade(color, -0.2));
  c.setLineWidth(3);
  c.strokeRect(p.x + 10, p.y + 10, p.w - 20, p.h - 20);
  c.setLineWidth(1);
  c.strokeRect(p.x + 16, p.y + 16, p.w - 32, p.h - 32);
  // A star at the top of the panel, and one at the bottom.
  for (double cy : {p.y + 10, p.y + p.h - 10}) {
    c.beginPath();
    star8(c, p.x + p.w / 2, cy, 15, M_PI / 8);
    c.setFill("#fffaf0");
    c.fill();
    c.setLineWidth(2);
    c.setStroke(shade(color, -0.2));
    c.stroke();
    c.beginPath();
    star8(c, p.x + p.w / 2, cy, 7, 0);
    c.setFill(color);
    c.fill();
  }
}

}  // namespace

const std::vector<CoverDesign>& coverDesigns() {
  static const std::vector<CoverDesign> d = {
      {"classic", "Classic", "cover"},     {"minimal", "Minimal", "cover-minimal"}, {"geometric", "Geometric", "cover-geometric"},
      {"waves", "Waves", "cover-waves"},   {"marble", "Marble", "cover-marble"},   {"leather", "Leather", "cover-leather"},
      {"kraft", "Kraft", "cover-kraft"},   {"dots", "Dots", "cover-dots"},         {"zellige", "Zellige", "cover-zellige"},
  };
  return d;
}

bool isCoverTemplate(const QString& t) { return t == "cover" || t.startsWith("cover-"); }

QString coverDesignOf(const QString& t) {
  for (const CoverDesign& d : coverDesigns())
    if (d.tmpl == t) return d.id;
  return "classic";
}

QString coverTemplate(const QString& design) {
  for (const CoverDesign& d : coverDesigns())
    if (d.id == design) return d.tmpl;
  return "cover";
}

QString shade(const QString& color, double amount) {
  static const QRegularExpression hex(QStringLiteral("^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$"), QRegularExpression::CaseInsensitiveOption);
  const auto m = hex.match(color.trimmed());
  if (!m.hasMatch()) return color;
  const double target = amount < 0 ? 0 : 255;
  const double k = std::abs(amount);
  auto ch = [&](int i) { return int(jsRound(m.captured(i).toInt(nullptr, 16) * (1 - k) + target * k)); };
  return QString("rgb(%1, %2, %3)").arg(ch(1)).arg(ch(2)).arg(ch(3));
}

CoverText coverText(const QString& design, double w, double h) {
  const QString darkTitle = "#1f2937", darkDate = "#6b7280";
  const QString lightTitle = "#ffffff", lightDate = "rgba(255, 255, 255, 0.78)";
  const double m = std::max(48.0, w * 0.08);
  if (design == "minimal") {
    const double y = h * (w > h ? 0.5 : 0.6);
    const double th = std::min(150.0, h * 0.16);
    return {{m, y, w - 2 * m, th}, {m, y + th + 14, w - 2 * m, 0}, "left", lightTitle, lightDate, "sans", 52, 16};
  }
  if (design == "geometric") {
    const double y = h * 0.66;
    const double th = std::min(130.0, h * 0.14);
    return {{m, y, w - 2 * m, th}, {m, y + th + 10, w - 2 * m, 0}, "left", lightTitle, lightDate, "sans", 44, 16};
  }
  if (design == "waves") {
    const double y = h * 0.16;
    const double th = std::min(140.0, h * 0.15);
    return {{m, y, w - 2 * m, th}, {m, y + th + 8, w - 2 * m, 0}, "center", lightTitle, lightDate, "sans", 46, 16};
  }
  if (design == "marble") {
    const Box p = marblePlate(w, h);
    return {{p.x + 24, p.y + 40, p.w - 48, p.h * 0.55 - 40}, {p.x + 24, p.y + p.h * 0.72 - 18, p.w - 48, 0}, "center", "#111827", "#4b5563", "mynerve", 40, 20};
  }
  if (design == "leather") {
    const double y = h * 0.36;
    const double th = std::min(130.0, h * 0.13);
    return {{m + 20, y, w - 2 * m - 40, th}, {m + 20, y + th + 34, w - 2 * m - 40, 0}, "center", "#e8cf8a", "rgba(232, 207, 138, 0.75)", "sans", 40, 15};
  }
  if (design == "kraft") {
    const Box p = kraftLabel(w, h);
    return {{p.x + 22, p.y + 22, p.w - 44, p.h * 0.62 - 22}, {p.x + 22, p.y + p.h * 0.66, p.w - 44, 0}, "center", "#3f2d1c", "#7a6047", "playpen", 38, 17};
  }
  if (design == "zellige") {
    const Box p = zelligePanel(w, h);
    return {{p.x + 30, p.y + 56, p.w - 60, std::min(120.0, p.h * 0.17)}, {p.x + 30, p.y + p.h - 58, p.w - 60, 0}, "center", darkTitle, darkDate, "sans", 44, 16};
  }
  if (design == "dots") {
    const Circle l = dotsLabel(w, h);
    const double tw = l.r * 1.5;
    return {{l.x - tw / 2, l.y - l.r * 0.42, tw, l.r * 0.62}, {l.x - tw / 2, l.y + l.r * 0.3, tw, 0}, "center", darkTitle, darkDate, "sans", 36, 15};
  }
  const ClassicLayout L = classicLayout(w, h);
  return {L.text.title, L.text.date, "center", darkTitle, darkDate, "sans", 34, 15};
}

void drawCoverDesign(JsCanvas& c, const QString& tmpl, double w, double h, const QString& color) {
  c.setFill(color);
  c.fillRect(0, 0, w, h);
  c.save();
  const QString d = coverDesignOf(tmpl);
  if (d == "classic") drawClassic(c, w, h, color);
  else if (d == "minimal") drawMinimal(c, w, h);
  else if (d == "geometric") drawGeometric(c, w, h, color);
  else if (d == "waves") drawWaves(c, w, h, color);
  else if (d == "marble") drawMarble(c, w, h, color);
  else if (d == "leather") drawLeather(c, w, h, color);
  else if (d == "kraft") drawKraft(c, w, h, color);
  else if (d == "dots") drawDots(c, w, h, color);
  else if (d == "zellige") drawZellige(c, w, h, color);
  c.restore();
}

}  // namespace draw
