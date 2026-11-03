#include "Paper.h"

namespace paper {
namespace {

bool isDark(const QColor& c) { return c.lightnessF() < 0.45; }

struct Inks {
  QColor line, margin, dot;
};

Inks inks(const QColor& paperColor) {
  if (isDark(paperColor)) return {QColor(170, 190, 225, int(0.28 * 255)), QColor(255, 120, 120, int(0.45 * 255)), QColor(200, 210, 225, int(0.4 * 255))};
  return {QColor(96, 140, 200, int(0.35 * 255)), QColor(220, 80, 80, int(0.45 * 255)), QColor(90, 100, 120, int(0.45 * 255))};
}

void rect(Layer& l, float x, float y, float w, float h) {
  const QPointF a(x, y), b(x + w, y), c(x + w, y + h), d(x, y + h);
  l.tris.insert(l.tris.end(), {a, b, c, a, c, d});
}

void hline(Layer& l, float y, float x0, float x1) { rect(l, x0, y - 0.5f, x1 - x0, 1.f); }
void vline(Layer& l, float x, float y0, float y1) { rect(l, x - 0.5f, y0, 1.f, y1 - y0); }

constexpr float kRuleTop = 110;

void ruled(std::vector<Layer>& out, const Inks& ink, float gap, float w, float h, float margin) {
  Layer lines{ink.line, {}}, m{ink.margin, {}};
  for (float y = kRuleTop; y < h - 30; y += gap) hline(lines, y, 0, w);
  vline(m, margin, 0, h);
  out.push_back(std::move(lines));
  out.push_back(std::move(m));
}

}  // namespace

bool supported(const QString& t) {
  static const QStringList ok = {"blank", "ruled", "narrow-ruled", "wide-ruled", "legal", "grid", "dotted"};
  return ok.contains(t);
}

std::vector<Layer> lines(const QString& t, float w, float h, const QColor& paperColor) {
  const Inks ink = inks(paperColor);
  std::vector<Layer> out;
  if (t == "ruled") ruled(out, ink, 34, w, h, 90);
  else if (t == "narrow-ruled") ruled(out, ink, 26, w, h, 90);
  else if (t == "wide-ruled") ruled(out, ink, 44, w, h, 90);
  else if (t == "legal") {
    ruled(out, ink, 34, w, h, 112);
    vline(out.back(), 118, 0, h);
  } else if (t == "grid") {
    Layer l{ink.line, {}};
    for (float y = 24; y < h; y += 24) hline(l, y, 0, w);
    for (float x = 24; x < w; x += 24) vline(l, x, 0, h);
    out.push_back(std::move(l));
  } else if (t == "dotted") {
    Layer l{ink.dot, {}};
    for (float y = 24; y < h; y += 24)
      for (float x = 24; x < w; x += 24) rect(l, x - 1, y - 1, 2.2f, 2.2f);
    out.push_back(std::move(l));
  }
  return out;
}

}  // namespace paper
