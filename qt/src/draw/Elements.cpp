#include "Elements.h"

#include "../math/MathExpr.h"

#include "Render.h"

#include <QRegularExpression>
#include <cmath>

#include "../Canvas.h"
#include "Fonts.h"
#include "Papers.h"
#include "Strokes.h"

namespace draw {
namespace {

using P2 = std::pair<double, double>;

// JavaScript's \s: Unicode white space and line terminators.
const QString kJsSpace = QStringLiteral("\\t\\n\\x{0B}\\f\\r \\x{A0}\\x{1680}\\x{2000}-\\x{200A}\\x{2028}\\x{2029}\\x{202F}\\x{205F}\\x{3000}\\x{FEFF}");

bool isJsSpace(QChar ch) {
  const char16_t u = ch.unicode();
  return u == 9 || u == 10 || u == 11 || u == 12 || u == 13 || u == 32 || u == 0xA0 || u == 0x1680 || (u >= 0x2000 && u <= 0x200A) || u == 0x2028 || u == 0x2029 || u == 0x202F || u == 0x205F || u == 0x3000 ||
         u == 0xFEFF;
}
QString trimEnd(const QString& s) {
  int n = int(s.size());
  while (n > 0 && isJsSpace(s[n - 1])) --n;
  return s.left(n);
}
QString trimStart(const QString& s) {
  int i = 0;
  while (i < s.size() && isJsSpace(s[i])) ++i;
  return s.mid(i);
}
bool blank(const QString& s) { return trimStart(s).isEmpty(); }

/// String.split(/(\s+)/): words and the space runs between them.
QStringList splitKeepSpaces(const QString& s) {
  static const QRegularExpression ws("[" + kJsSpace + "]+");
  QStringList out;
  int at = 0;
  auto it = ws.globalMatch(s);
  while (it.hasNext()) {
    const auto m = it.next();
    out << s.mid(at, m.capturedStart() - at) << m.captured(0);
    at = int(m.capturedEnd());
  }
  out << s.mid(at);
  return out;
}

/// A list item's marker ("• ", "1. "…) at the start of a paragraph.
QString listMarker(const QString& para) {
  static const QRegularExpression re("^(•|◦|-|\\*|[0-9]{1,3}[.)])[" + kJsSpace + "]+");
  const auto m = re.match(para);
  return m.hasMatch() ? m.captured(0) : QString();
}

QStringList wrapLines(Canvas& c, const QString& text, double maxWidth) {
  QStringList out;
  for (const QString& para : text.split('\n')) {
    QString line;
    for (const QString& word : splitKeepSpaces(para)) {
      const QString candidate = line + word;
      if (!line.isEmpty() && c.textWidth(candidate) > maxWidth) {
        out << trimEnd(line);
        line = trimStart(word);
      } else {
        line = candidate;
      }
    }
    out << line;
  }
  return out;
}

Canvas& measurer() {
  thread_local Canvas* m = new Canvas(300, 150);  // per thread: previews draw on workers
  return *m;
}

/// Draws a laid-out line, right to left for Arabic (x is always its left edge).
void fillLine(Canvas& c, const LaidLine& line) {
  c.setFont(line.font);
  c.setDirection(line.rtl ? "rtl" : "ltr");
  c.setTextAlign("left");
  c.fillText(line.text, line.x, line.y);
}

std::vector<P2> ptsOf(const QJsonArray& a) {
  std::vector<P2> out;
  for (const QJsonValue& v : a) out.push_back({v.toArray()[0].toDouble(), v.toArray()[1].toDouble()});
  return out;
}

bool isClosedShape(const QJsonObject& s) {
  const QString k = s["kind"].toString();
  if (k == "line" || k == "arrow") return false;
  if (k == "path") return s["closed"].toBool();
  return true;
}

void drawArrowHead(Canvas& c, P2 from, P2 to, double width) {
  const double angle = std::atan2(to.second - from.second, to.first - from.first);
  const double len = std::max(12.0, width * 4.5);
  c.beginPath();
  for (int side : {-1, 1}) {
    const double a = angle + M_PI - side * 0.45;
    c.moveTo(to.first, to.second);
    c.lineTo(to.first + std::cos(a) * len, to.second + std::sin(a) * len);
  }
  c.stroke();
}

void drawShape(Canvas& c, const QJsonObject& el) {
  const QJsonObject s = el["shape"].toObject();
  const QString kind = s["kind"].toString();
  const double width = el["width"].toDouble();
  c.save();
  c.setLineWidth(width);
  c.setLineCap("round");
  c.setLineJoin("round");
  c.setStroke(el["color"].toString());
  c.beginPath();
  const std::vector<P2> pts = ptsOf(s["points"].toArray());
  if (kind == "ellipse") {
    c.ellipse(s["cx"].toDouble(), s["cy"].toDouble(), std::max(s["rx"].toDouble(), 0.5), std::max(s["ry"].toDouble(), 0.5), 0, 0, M_PI * 2);
  } else if (kind == "path" && !s["breaks"].toArray().isEmpty()) {
    // A multi-stroke symbol: each stroke on its own.
    std::vector<int> cuts{0};
    for (const QJsonValue& b : s["breaks"].toArray()) cuts.push_back(b.toInt());
    cuts.push_back(int(pts.size()));
    for (size_t i = 0; i + 1 < cuts.size(); i++) {
      if (cuts[i + 1] <= cuts[i]) continue;
      c.moveTo(pts[size_t(cuts[i])].first, pts[size_t(cuts[i])].second);
      for (int k = cuts[i] + 1; k < cuts[i + 1]; ++k) c.lineTo(pts[size_t(k)].first, pts[size_t(k)].second);
    }
  } else if (!pts.empty()) {
    c.moveTo(pts[0].first, pts[0].second);
    for (size_t i = 1; i < pts.size(); ++i) c.lineTo(pts[i].first, pts[i].second);
    if (isClosedShape(s)) c.closePath();
  }
  const QString fill = el["fill"].toString();
  if (!fill.isEmpty() && isClosedShape(s)) {
    c.setFill(fill);
    c.fill();
  }
  c.stroke();
  if (kind == "arrow" && pts.size() >= 2) drawArrowHead(c, pts[pts.size() - 2], pts.back(), width);
  c.restore();
}

void drawText(Canvas& c, const QJsonObject& el) {
  c.save();
  c.setFill(el["color"].toString());
  c.setTextBaseline("top");
  const double size = el["fontSize"].toDouble();
  for (const LaidLine& line : layoutText(c, el["text"].toString(), el["x"].toDouble(), el["y"].toDouble(), el["w"].toDouble(), size, size * 1.35, TextStyle::of(el))) fillLine(c, line);
  c.restore();
}

void drawSticky(Canvas& c, const QJsonObject& el) {
  const double x = el["x"].toDouble(), y = el["y"].toDouble(), w = el["w"].toDouble(), h = el["h"].toDouble();
  c.save();
  c.setShadowColor("rgba(0,0,0,0.18)");
  c.setShadowBlur(10);
  c.setShadowOffsetY(3);
  c.setFill(el["color"].toString());
  c.fillRect(x, y, w, h);
  c.setShadowColor("transparent");
  c.setFill("#1f2937");
  const double size = el["fontSize"].isDouble() ? el["fontSize"].toDouble() : 18;
  const double lineH = size * 1.33;
  c.setTextBaseline("top");
  const std::vector<LaidLine> lines = layoutText(c, el["text"].toString(), x + 12, y + 12, w - 24, size, lineH, TextStyle::of(el));
  const size_t n = size_t(std::max(1.0, std::floor((h - 24) / lineH)));
  for (size_t i = 0; i < lines.size() && i < n; ++i) fillLine(c, lines[i]);
  c.restore();
}

void drawImage(Canvas& c, const QJsonObject& el) {
  const double x = el["x"].toDouble(), y = el["y"].toDouble(), w = el["w"].toDouble(), h = el["h"].toDouble();
  QImage img;
  if (const QImage* im = imageFor(el["src"].toString())) img = *im;
  if (!img.isNull() && img.width() > 0) {
    c.drawImage(img, x, y, w, h);
  } else {
    c.save();
    c.setStroke("#9ca3af");
    c.setDash({6, 4});
    c.strokeRect(x, y, w, h);
    c.restore();
  }
}

/// A tidy step for ticks: 1, 2 or 5 × 10ⁿ, about `span / want` long.
double niceStep(double span, double want) {
  const double raw = span / std::max(1.0, want);
  const double p = std::pow(10.0, std::floor(std::log10(raw)));
  for (double m : {1.0, 2.0, 5.0, 10.0})
    if (raw <= m * p) return m * p;
  return 10 * p;
}

/// A graph: a white card with a grid, axes and labelled ticks, and each
/// function (one per line of its text) as a curve, broken where it isn't
/// defined or jumps (1/x, tan x). Ranges in xmin, xmax, ymin, ymax.
void drawGraph(Canvas& c, const QJsonObject& el) {
  const double x = el["x"].toDouble(), y = el["y"].toDouble(), w = el["w"].toDouble(), h = el["h"].toDouble();
  if (w < 4 || h < 4) return;
  double x0 = el["xmin"].toDouble(-5), x1 = el["xmax"].toDouble(5), y0 = el["ymin"].toDouble(-5), y1 = el["ymax"].toDouble(5);
  if (!(x1 > x0)) x1 = x0 + 1;
  if (!(y1 > y0)) y1 = y0 + 1;
  const bool grid = el["grid"].toBool(true);
  // Page units for a graph value.
  auto px = [&](double v) { return x + (v - x0) / (x1 - x0) * w; };
  auto py = [&](double v) { return y + h - (v - y0) / (y1 - y0) * h; };
  const double fs = std::clamp(std::min(w, h) / 26, 7.0, 14.0);
  c.save();
  // The card.
  c.setFill("#ffffff");
  c.beginPath();
  c.roundRect(x, y, w, h, QJSValue(6));
  c.fill();
  c.setStroke("#d1d5db");
  c.setLineWidth(1);
  c.stroke();
  c.beginPath();
  c.rect(x, y, w, h);
  c.clip();
  const double sx = niceStep(x1 - x0, std::max(4.0, w / 45)), sy = niceStep(y1 - y0, std::max(4.0, h / 45));
  if (grid) {
    c.setStroke("#e5e7eb");
    c.setLineWidth(0.75);
    c.beginPath();
    for (double v = std::ceil(x0 / sx) * sx; v <= x1 + 1e-9; v += sx) c.moveTo(px(v), y), c.lineTo(px(v), y + h);
    for (double v = std::ceil(y0 / sy) * sy; v <= y1 + 1e-9; v += sy) c.moveTo(x, py(v)), c.lineTo(x + w, py(v));
    c.stroke();
  }
  // The axes (at 0, or along the edge when 0 is off the graph), with arrows.
  const double ax = std::clamp(py(0), y, y + h), ay = std::clamp(px(0), x, x + w);
  c.setStroke("#374151");
  c.setLineWidth(1.3);
  c.beginPath();
  c.moveTo(x, ax), c.lineTo(x + w, ax);
  c.moveTo(ay, y + h), c.lineTo(ay, y);
  c.stroke();
  const double ar = fs * 0.55;
  c.setFill("#374151");
  c.beginPath();
  c.moveTo(x + w, ax), c.lineTo(x + w - ar * 1.4, ax - ar * 0.7), c.lineTo(x + w - ar * 1.4, ax + ar * 0.7);
  c.moveTo(ay, y), c.lineTo(ay - ar * 0.7, y + ar * 1.4), c.lineTo(ay + ar * 0.7, y + ar * 1.4);
  c.fill();
  // Tick labels (decimal commas, as at school).
  c.setFont(QStringLiteral("%1px \"Inter\", sans-serif").arg(jsNumber(fs)));
  c.setFill("#6b7280");
  auto label = [&](double v) { return mathx::format(v, true, false, 4); };
  c.setTextAlign("center");
  c.setTextBaseline("top");
  for (double v = std::ceil(x0 / sx) * sx; v <= x1 + 1e-9; v += sx) {
    if (std::abs(v) < sx / 2 || px(v) > x + w - fs * 2) continue;
    c.beginPath();
    c.moveTo(px(v), ax - 3), c.lineTo(px(v), ax + 3);
    c.stroke();
    c.fillText(label(v), px(v), std::min(ax + 4, y + h - fs * 1.3));
  }
  c.setTextAlign("right");
  c.setTextBaseline("middle");
  for (double v = std::ceil(y0 / sy) * sy; v <= y1 + 1e-9; v += sy) {
    if (std::abs(v) < sy / 2 || py(v) < y + fs * 2) continue;
    c.beginPath();
    c.moveTo(ay - 3, py(v)), c.lineTo(ay + 3, py(v));
    c.stroke();
    c.fillText(label(v), std::max(ay - 5, x + fs * 2.4), py(v));
  }
  c.setTextAlign("left");
  c.setTextBaseline("top");
  c.fillText(QStringLiteral("0"), ay + 3, std::min(ax + 3, y + h - fs * 1.3));
  // The curves, and a legend in their colours.
  static const QStringList palette = {"#2563eb", "#dc2626", "#16a34a", "#9333ea", "#ea580c"};
  const QJsonArray colors = el["colors"].toArray();
  const QStringList lines = el["text"].toString().split('\n', Qt::SkipEmptyParts);
  const int n = std::max(2, int(w * 1.5));
  const double jump = (y1 - y0) * 3;
  double legendY = y + 6;
  for (int i = 0; i < lines.size(); ++i) {
    const QString col = i < colors.size() && !colors[i].toString().isEmpty() ? colors[i].toString() : palette[i % palette.size()];
    const mathx::Function f = mathx::function(lines[i]);
    c.setFont(QStringLiteral("600 %1px \"Inter\", sans-serif").arg(jsNumber(fs * 1.05)));
    c.setFill(col);
    c.setTextAlign("left");
    c.setTextBaseline("top");
    QString name = lines[i].trimmed();
    if (!name.contains('=')) name = "y = " + name;
    c.fillText(f.ok() ? mathx::pretty(name) : name + "  (?)", x + 8, legendY);
    legendY += fs * 1.45;
    if (!f.ok()) continue;
    c.setStroke(col);
    c.setLineWidth(std::clamp(std::min(w, h) / 160, 1.6, 3.0));
    c.setLineCap("round");
    c.setLineJoin("round");
    c.beginPath();
    bool down = false;
    double prev = NAN;
    for (int k = 0; k <= n; ++k) {
      const double xv = x0 + (x1 - x0) * k / n;
      const double yv = f.at(xv);
      if (!std::isfinite(yv) || (std::isfinite(prev) && std::abs(yv - prev) > jump)) {
        down = false;
        prev = yv;
        continue;
      }
      // Far outside: clamped so the line still leaves the card the right way.
      const double yc = std::clamp(yv, y0 - (y1 - y0), y1 + (y1 - y0));
      if (down) c.lineTo(px(xv), py(yc));
      else c.moveTo(px(xv), py(yc));
      down = true;
      prev = yv;
    }
    c.stroke();
  }
  c.restore();
}

}  // namespace

TextStyle TextStyle::of(const QJsonObject& el) { return {el["font"].toString(), el["bold"].toBool(), el["italic"].toBool(), el["align"].toString()}; }

std::vector<LaidLine> layoutText(Canvas& c, const QString& text, double x, double y, double w, double size, double lineH, const TextStyle& style) {
  std::vector<LaidLine> out;
  int row = 0;
  for (const QString& para : text.split('\n')) {
    const bool rtl = isRtl(para);
    const QString font = fontCss(style.font, size, style.bold, style.italic, rtl);
    c.setFont(font);
    const QString m = listMarker(para);
    const double indent = m.isEmpty() ? 0 : c.textWidth(m);
    const QString body = m.isEmpty() ? para : para.mid(m.size());
    // Alignment is logical: "left" is the paragraph's start, which is the
    // right edge in Arabic. List indents go on the start side too.
    const QString align = rtl ? (style.align == "right" ? "left" : style.align == "center" ? "center" : "right") : (style.align.isEmpty() ? "left" : style.align);
    const QStringList lines = wrapLines(c, body, std::max(10.0, w - indent));
    for (int i = 0; i < lines.size(); ++i) {
      const QString t = i == 0 && !m.isEmpty() ? m + lines[i] : lines[i];
      const double lw = c.textWidth(t);
      const double baseX = i == 0 || rtl ? x : x + indent;
      const double avail = i == 0 ? w : w - indent;
      const double lx = align == "center" ? baseX + (avail - lw) / 2 : align == "right" ? baseX + avail - lw : baseX;
      out.push_back({t, lx, y + row * lineH, lw, rtl, font});
      row++;
    }
  }
  return out;
}

std::vector<LaidLine> layoutText(const QString& text, double x, double y, double w, double size, double lineH, const TextStyle& style) { return layoutText(measurer(), text, x, y, w, size, lineH, style); }

QRectF textInk(const QString& text, double x, double y, double w, double size, const TextStyle& style) {
  Canvas& c = measurer();
  QRectF out;
  for (const LaidLine& line : layoutText(c, text, x, y, w, size, size * 1.35, style)) {
    // As fillLine draws it.
    c.save();
    c.setFont(line.font);
    c.setDirection(line.rtl ? "rtl" : "ltr");
    c.setTextAlign("left");
    c.setTextBaseline("top");
    const QRectF r = c.textInk(line.text, line.x, line.y);
    c.restore();
    if (!r.isEmpty()) out = out.isNull() ? r : out.united(r);
  }
  return out;
}

double textHeight(const QString& text, double fontSize, double width, const TextStyle& style) {
  return std::max(1.0, double(layoutText(text, 0, 0, width, fontSize, fontSize, style).size())) * fontSize * 1.35;
}

QRectF boxOf(const QJsonObject& el) {
  const double x = el["x"].toDouble(), y = el["y"].toDouble(), w = el["w"].toDouble();
  if (el["kind"].toString() == "text") return {x, y, w, textHeight(el["text"].toString(), el["fontSize"].toDouble(), w, TextStyle::of(el))};
  return {x, y, w, el["h"].toDouble()};
}

double measureText(const QString& text, const QString& font, double size) {
  double best = -std::numeric_limits<double>::infinity();
  for (const QString& line : text.split('\n')) {
    measurer().setFont(fontCss(font, size, false, false, isRtl(line)));
    best = std::max(best, measurer().textWidth(line));
  }
  return best;
}

double naturalTextWidth(const QJsonObject& el) {
  double best = 0;
  const double size = el["fontSize"].toDouble();
  for (const LaidLine& l : layoutText(el["text"].toString(), 0, 0, 1e6, size, size, TextStyle::of(el))) best = std::max(best, l.width);
  return best;
}

void drawElement(Canvas& c, const QJsonObject& el) {
  const QString kind = el["kind"].toString();
  // Text boxes, sticky notes and pictures can be rotated around their centre.
  if ((kind == "text" || kind == "sticky" || kind == "image" || kind == "graph") && el["rotation"].toDouble() != 0) {
    const QRectF r = boxOf(el);
    const double cx = r.x() + r.width() / 2, cy = r.y() + r.height() / 2;
    c.save();
    c.translate(cx, cy);
    c.rotate(el["rotation"].toDouble());
    c.translate(-cx, -cy);
    QJsonObject flat = el;
    flat["rotation"] = 0;
    drawElement(c, flat);
    c.restore();
    return;
  }
  if (kind == "stroke") drawStroke(c, el);
  else if (kind == "shape") drawShape(c, el);
  else if (kind == "text") drawText(c, el);
  else if (kind == "sticky") drawSticky(c, el);
  else if (kind == "image") drawImage(c, el);
  else if (kind == "graph") drawGraph(c, el);
}

void paintPage(Canvas& c, const QString& tmpl, const QString& paper, double w, double h, const QJsonArray& els, const QImage& background) {
  drawPaper(c, tmpl, w, h, paper);
  if (!background.isNull()) c.drawImage(background, 0, 0, w, h);
  for (const QJsonValue& v : els) drawElement(c, v.toObject());
}

std::unique_ptr<Canvas> renderPage(const QString& tmpl, const QString& paper, double w, double h, const QJsonArray& els, double scale, const QImage& background) {
  auto c = std::make_unique<Canvas>(std::max(1, int(std::floor(w * scale + 0.5))), std::max(1, int(std::floor(h * scale + 0.5))));
  c->scale(scale, scale);
  paintPage(*c, tmpl, paper, w, h, els, background);
  return c;
}

}  // namespace draw
