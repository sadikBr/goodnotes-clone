#include "MathKit.h"

#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <vector>

#include "../Canvas.h"
#include "../draw/Elements.h"
#include "MathExpr.h"

QVariantMap MathKit::calculate(const QString& text) const {
  const mathx::Result r = mathx::calculate(text);
  return {{"ok", r.ok()}, {"kind", r.kind}, {"answer", r.answer}, {"pretty", r.pretty}, {"var", r.var}, {"error", r.error}};
}

QString MathKit::pretty(const QString& text) const { return mathx::pretty(text); }

QVariantList MathKit::checkFunctions(const QString& lines) const {
  QVariantList out;
  for (const QString& line : lines.split('\n', Qt::SkipEmptyParts)) {
    const mathx::Function f = mathx::function(line);
    out << QVariantMap{{"ok", f.ok()}, {"error", f.error}};
  }
  return out;
}

QVariantMap MathKit::fitY(const QString& lines, double xmin, double xmax) const {
  std::vector<double> ys;
  for (const QString& line : lines.split('\n', Qt::SkipEmptyParts)) {
    const mathx::Function f = mathx::function(line);
    if (!f.ok()) continue;
    for (int i = 0; i <= 400; ++i) {
      const double v = f.at(xmin + (xmax - xmin) * i / 400);
      if (std::isfinite(v)) ys.push_back(v);
    }
  }
  if (ys.empty()) return {};
  std::sort(ys.begin(), ys.end());
  // The middle 96% (1/x near 0 would squash everything else).
  double lo = ys[size_t(ys.size() * 0.02)], hi = ys[std::min(ys.size() - 1, size_t(ys.size() * 0.98))];
  if (hi - lo < 1e-9) lo -= 1, hi += 1;
  const double pad = (hi - lo) * 0.12;
  lo -= pad;
  hi += pad;
  // Round to a tidy step; 0 shown when it's close.
  const double step = std::pow(10.0, std::floor(std::log10(hi - lo))) / 2;
  lo = std::floor(lo / step) * step;
  hi = std::ceil(hi / step) * step;
  if (lo > 0 && lo < (hi - lo) * 0.3) lo = 0;
  if (hi < 0 && -hi < (hi - lo) * 0.3) hi = 0;
  return {{"ymin", lo}, {"ymax", hi}};
}

QString MathKit::graphPicture(const QVariantMap& spec, int w, int h) const {
  if (w < 8 || h < 8) return {};
  QJsonObject el = QJsonObject::fromVariantMap(spec);
  el["kind"] = "graph";
  el["text"] = spec.value("functions").toString();
  el["x"] = 0;
  el["y"] = 0;
  el["w"] = w;
  el["h"] = h;
  Canvas c(w * 2, h * 2);
  c.scale(2, 2);
  draw::drawElement(c, el);
  return c.toDataURL("image/png");
}
