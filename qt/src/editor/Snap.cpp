#include "Snap.h"

#include <QHash>
#include <cmath>
#include <limits>
#include <optional>

#include "../Canvas.h"
#include "../draw/Elements.h"

namespace ed {
namespace {

bool pairs(const QString& anchor, const QString& target) {
  if (anchor == "baseline") return target == "baseline" || target == "rule";
  if (anchor == "mid") return target == "mid" || target == "rulemid";
  if (anchor == "left") return target == "left" || target == "margin";
  if (anchor == "right") return target == "right";
  if (anchor == "center") return target == "center" || target == "pagecenter";
  if (anchor == "middle") return target == "middle" || target == "pagemiddle";
  if (anchor == "boxleft" || anchor == "boxright" || anchor == "boxtop" || anchor == "boxbottom") return target == anchor;
  return false;
}

QString label(const QString& t) {
  static const QHash<QString, QString> l = {{"baseline", "Baseline"}, {"mid", "Text centre"}, {"left", "Left edge"},  {"right", "Right edge"},     {"center", "Centre"},
                                            {"rule", "On the line"},  {"rulemid", "Centred in line"}, {"margin", "Margin"}, {"pagecenter", "Page centre"},
                                            {"middle", "Middle"}, {"pagemiddle", "Page middle"}, {"boxleft", "Left edges"}, {"boxright", "Right edges"},
                                            {"boxtop", "Top edges"}, {"boxbottom", "Bottom edges"}};
  return l.value(t);
}

std::optional<double> bestOffset(const std::vector<Anchor>& anchors, const std::vector<Target>& targets, double delta, double threshold) {
  std::optional<double> best;
  for (const Anchor& a : anchors)
    for (const Target& t : targets) {
      if (t.axis != a.axis || !pairs(a.kind, t.kind)) continue;
      const double off = t.value - (a.value + delta);
      if (std::abs(off) <= threshold && (!best || std::abs(off) < std::abs(*best))) best = off;
    }
  return best;
}

}  // namespace

SnapResult snapMove(const std::vector<Anchor>& anchors, const std::vector<Target>& targets, double dx, double dy, double threshold) {
  std::vector<Anchor> xs, ys;
  for (const Anchor& a : anchors) (a.axis == 'x' ? xs : ys).push_back(a);
  const auto ox = bestOffset(xs, targets, dx, threshold);
  const auto oy = bestOffset(ys, targets, dy, threshold);
  const double ndx = dx + ox.value_or(0), ndy = dy + oy.value_or(0);
  SnapResult out{ndx, ndy, {}};
  // One guide per line (a centred title and the page centre on the same
  // line: one guide, the page's name on it, long enough for both).
  QHash<QString, size_t> line;
  for (const Anchor& a : anchors) {
    const double delta = a.axis == 'x' ? ndx : ndy;
    const double shift = a.axis == 'x' ? ndy : ndx;  // the span runs along the other axis
    if (!(a.axis == 'x' ? ox : oy)) continue;
    for (const Target& t : targets) {
      if (t.axis != a.axis || !pairs(a.kind, t.kind)) continue;
      if (std::abs(t.value - (a.value + delta)) > 0.5) continue;
      const QString key = QString("%1:%2").arg(t.axis).arg(t.value, 0, 'f', 1);
      const double from = std::min(a.from + shift, t.from), to = std::max(a.to + shift, t.to);
      if (auto it = line.find(key); it != line.end()) {
        Guide& g = out.guides[*it];
        g.from = std::min(g.from, from);
        g.to = std::max(g.to, to);
        if (t.kind.startsWith("page") || t.kind.startsWith("rule") || t.kind == "margin") g.label = label(t.kind);
        continue;
      }
      line.insert(key, out.guides.size());
      Guide g{t.axis, t.value, from, to, label(t.kind), 0, 0};
      if (a.axis == 'y') {
        g.labelX = a.to + shift + 8;
        g.labelY = t.value;
      } else {
        g.labelX = t.value + 4;
        g.labelY = a.from + shift - 6;
      }
      out.guides.push_back(g);
    }
  }
  return out;
}

std::vector<Target> paperTargets(const std::vector<double>& rules, double spanFrom, double spanTo, bool hasMargin, double margin, double pageW, double pageH) {
  std::vector<Target> out;
  for (double y : rules) out.push_back({'y', "rule", y, spanFrom, spanTo});
  for (size_t i = 1; i < rules.size(); i++) out.push_back({'y', "rulemid", (rules[i - 1] + rules[i]) / 2, spanFrom, spanTo});
  if (hasMargin) out.push_back({'x', "margin", margin, 0, pageH});
  out.push_back({'x', "pagecenter", pageW / 2, 0, pageH});
  out.push_back({'y', "pagemiddle", pageH / 2, 0, pageW});
  return out;
}

std::vector<Anchor> boxAnchors(const Rect& r) {
  const double x1 = r.x + r.w, y1 = r.y + r.h;
  return {{'x', "boxleft", r.x, r.y, y1}, {'x', "center", r.x + r.w / 2, r.y, y1}, {'x', "boxright", x1, r.y, y1},
          {'y', "boxtop", r.y, r.x, x1},  {'y', "middle", r.y + r.h / 2, r.x, x1}, {'y', "boxbottom", y1, r.x, x1}};
}

std::vector<Anchor> textAnchors(const El& el) {
  static Canvas* ctx = new Canvas(300, 150);
  const draw::TextStyle ts{el.font.value_or(QString()), el.bold.value_or(false), el.italic.value_or(false), el.align.value_or(QString())};
  const double size = el.fontSize.value_or(18);
  const auto lines = draw::layoutText(*ctx, el.text, el.x, el.y, el.w, size, size * 1.35, ts);
  ctx->setTextBaseline("top");
  const double baseOff = ctx->measureText("H").value("actualBoundingBoxDescent").toDouble();
  const QVariantMap xm = ctx->measureText("x");
  const double midOff = (xm.value("actualBoundingBoxDescent").toDouble() - xm.value("actualBoundingBoxAscent").toDouble()) / 2;
  std::vector<Anchor> out;
  const double INF = std::numeric_limits<double>::infinity();
  double left = INF, right = -INF, top = INF, bottom = -INF;
  for (const draw::LaidLine& line : lines) {
    if (line.text.trimmed().isEmpty()) continue;
    ctx->setFont(line.font);
    const QVariantMap m = ctx->measureText(line.text);
    const double l = line.x - m.value("actualBoundingBoxLeft").toDouble();
    const double r = line.x + m.value("actualBoundingBoxRight").toDouble();
    out.push_back({'y', "baseline", line.y + baseOff, l, r});
    out.push_back({'y', "mid", line.y + midOff, l, r});
    left = std::min(left, l);
    right = std::max(right, r);
    top = std::min(top, line.y - m.value("actualBoundingBoxAscent").toDouble());
    bottom = std::max(bottom, line.y + m.value("actualBoundingBoxDescent").toDouble());
  }
  ctx->setTextBaseline("alphabetic");
  if (!out.empty()) {
    out.push_back({'x', "left", left, top, bottom});
    out.push_back({'x', "right", right, top, bottom});
    out.push_back({'x', "center", (left + right) / 2, top, bottom});
  }
  return out;
}

}  // namespace ed
