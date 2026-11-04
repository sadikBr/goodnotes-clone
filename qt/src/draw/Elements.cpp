#include "Elements.h"

#include "Render.h"

#include <QRegularExpression>
#include <cmath>

#include "../Js.h"
#include "../JsCanvas.h"
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

QStringList wrapLines(JsCanvas& c, const QString& text, double maxWidth) {
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

JsCanvas& measurer() {
  static JsCanvas* m = new JsCanvas(300, 150);
  return *m;
}

/// Draws a laid-out line, right to left for Arabic (x is always its left edge).
void fillLine(JsCanvas& c, const LaidLine& line) {
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

void drawArrowHead(JsCanvas& c, P2 from, P2 to, double width) {
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

void drawShape(JsCanvas& c, const QJsonObject& el) {
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

void drawText(JsCanvas& c, const QJsonObject& el) {
  c.save();
  c.setFill(el["color"].toString());
  c.setTextBaseline("top");
  const double size = el["fontSize"].toDouble();
  for (const LaidLine& line : layoutText(c, el["text"].toString(), el["x"].toDouble(), el["y"].toDouble(), el["w"].toDouble(), size, size * 1.35, TextStyle::of(el))) fillLine(c, line);
  c.restore();
}

void drawSticky(JsCanvas& c, const QJsonObject& el) {
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

void drawImage(JsCanvas& c, const QJsonObject& el) {
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

}  // namespace

TextStyle TextStyle::of(const QJsonObject& el) { return {el["font"].toString(), el["bold"].toBool(), el["italic"].toBool(), el["align"].toString()}; }

std::vector<LaidLine> layoutText(JsCanvas& c, const QString& text, double x, double y, double w, double size, double lineH, const TextStyle& style) {
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

void drawElement(JsCanvas& c, const QJsonObject& el) {
  const QString kind = el["kind"].toString();
  // Text boxes, sticky notes and pictures can be rotated around their centre.
  if ((kind == "text" || kind == "sticky" || kind == "image") && el["rotation"].toDouble() != 0) {
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
}

void paintPage(JsCanvas& c, const QString& tmpl, const QString& paper, double w, double h, const QJsonArray& els, const QImage& background) {
  drawPaper(c, tmpl, w, h, paper);
  if (!background.isNull()) c.drawImage(background, 0, 0, w, h);
  for (const QJsonValue& v : els) drawElement(c, v.toObject());
}

std::unique_ptr<JsCanvas> renderPage(const QString& tmpl, const QString& paper, double w, double h, const QJsonArray& els, double scale, const QImage& background) {
  auto c = std::make_unique<JsCanvas>(std::max(1, int(std::floor(w * scale + 0.5))), std::max(1, int(std::floor(h * scale + 0.5))));
  c->scale(scale, scale);
  paintPage(*c, tmpl, paper, w, h, els, background);
  return c;
}

}  // namespace draw
