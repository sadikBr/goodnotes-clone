#include "Papers.h"

#include <QRegularExpression>
#include <array>
#include <cmath>

#include "../JsCanvas.h"
#include "Covers.h"
#include "Fonts.h"

namespace draw {
namespace {

/// Math.round as JavaScript does it (halves round up).
double jsRound(double v) { return std::floor(v + 0.5); }

struct Inks {
  QString line, strong, faint, margin, dot, text;
};

Inks inks(const QString& paper) {
  if (isDarkPaper(paper))
    return {"rgba(170, 190, 225, 0.28)", "rgba(190, 205, 235, 0.5)", "rgba(170, 190, 225, 0.15)", "rgba(255, 120, 120, 0.45)", "rgba(200, 210, 225, 0.4)", "rgba(205, 215, 230, 0.72)"};
  return {"rgba(96, 140, 200, 0.35)", "rgba(70, 110, 170, 0.6)", "rgba(96, 140, 200, 0.18)", "rgba(220, 80, 80, 0.45)", "rgba(90, 100, 120, 0.45)", "rgba(80, 92, 112, 0.8)"};
}

// --- shared geometry --------------------------------------------------------

constexpr double RULE_TOP = 110;

std::vector<double> ruledRows(double gap, double h, double top = RULE_TOP, double bottom = -1) {
  if (bottom < 0) bottom = h - 30;
  std::vector<double> out;
  for (double y = top; y < bottom; y += gap) out.push_back(y);
  return out;
}

struct HandRow {
  double top, mid, base, desc;
};
/// Handwriting practice rows: ascender, dashed middle, baseline, descender.
std::vector<HandRow> handwritingRows(double h) {
  const double body = 44, desc = 18, gap = 26;
  std::vector<HandRow> rows;
  for (double top = 80; top + body + desc < h - 30; top += body + desc + gap) rows.push_back({top, top + body / 2, top + body, top + body + desc});
  return rows;
}

struct CalliRow {
  double asc, xh, base, desc;
};
/// Calligraphy rows: ascender, x-height, baseline, descender (x-height 22).
std::vector<CalliRow> calligraphyRows(double h) {
  const double x = 22, asc = x * 2, desc = x * 1.5, gap = 18;
  std::vector<CalliRow> rows;
  for (double base = 70 + asc; base + desc < h - 30; base += asc + desc + gap) rows.push_back({base - x - asc, base - x, base, base + desc});
  return rows;
}

std::vector<double> twoColumnRules(double h) { return ruledRows(34, h, 144); }

// --- drawing ----------------------------------------------------------------

/// Collects line segments and strokes them as one path (crossings don't darken).
class Lines {
public:
  Lines& add(double x0, double y0, double x1, double y1) {
    m_segs.push_back({x0, y0, x1, y1});
    return *this;
  }
  Lines& h(double y, double x0, double x1) { return add(x0, y + 0.5, x1, y + 0.5); }
  Lines& v(double x, double y0, double y1) { return add(x + 0.5, y0, x + 0.5, y1); }
  Lines& rect(double x, double y, double w, double hh) { return h(y, x, x + w).h(y + hh, x, x + w).v(x, y, y + hh).v(x + w, y, y + hh); }
  void stroke(JsCanvas& c, const QString& color, double width = 1, std::initializer_list<qreal> dash = {}) {
    if (m_segs.empty()) return;
    c.save();
    c.setStroke(color);
    c.setLineWidth(width);
    c.setLineCap("butt");
    c.setDash(dash);
    c.beginPath();
    for (const auto& s : m_segs) {
      c.moveTo(s[0], s[1]);
      c.lineTo(s[2], s[3]);
    }
    c.stroke();
    c.restore();
    m_segs.clear();
  }

private:
  std::vector<std::array<double, 4>> m_segs;
};

void label(JsCanvas& c, const QString& text, double x, double y, const Inks& ink, double size = 13, bool bold = false) {
  c.save();
  c.setFont(fontCss("sans", size, bold, false, isRtl(text)));
  c.setFill(ink.text);
  c.setTextBaseline("alphabetic");
  c.fillText(text, x, y);
  c.restore();
}

void checkbox(Lines& lines, double x, double baseline, double size = 14) { lines.rect(x, baseline - size - 5, size, size); }

/// Millimetre paper: faint 1 mm lines, medium 5 mm lines and bolder 1 cm
/// lines. One centimetre line passes through (ox, oy).
void drawMillimetreGrid(JsCanvas& c, double w, double h, const Inks& ink, double ox, double oy) {
  Lines mm, half, cm;
  auto level = [&](int k) -> Lines& { return k % 10 == 0 ? cm : k % 5 == 0 ? half : mm; };
  auto first = [](double o) { return -int(std::floor(o / MM)); };
  for (int k = first(ox); ox + k * MM <= w; k++) level(((k % 10) + 10) % 10).add(ox + k * MM, 0, ox + k * MM, h);
  for (int k = first(oy); oy + k * MM <= h; k++) level(((k % 10) + 10) % 10).add(0, oy + k * MM, w, oy + k * MM);
  mm.stroke(c, ink.faint, 0.45);
  half.stroke(c, ink.faint, 0.9);
  cm.stroke(c, ink.line, 1);
}

void drawGraph(JsCanvas& c, double w, double h, const Inks& ink) {
  // Centre the centimetre grid on the page.
  const double cmU = 10 * MM;
  drawMillimetreGrid(c, w, h, ink, std::fmod(w, cmU) / 2, std::fmod(h, cmU) / 2);
}

void drawCoordinate(JsCanvas& c, double w, double h, const Inks& ink) {
  const double cmU = 10 * MM;
  // Origin on the centimetre line nearest the page centre.
  const double ox = std::fmod(w, cmU) / 2, oy = std::fmod(h, cmU) / 2;
  const double cx = ox + jsRound((w / 2 - ox) / cmU) * cmU;
  const double cy = oy + jsRound((h / 2 - oy) / cmU) * cmU;
  drawMillimetreGrid(c, w, h, ink, ox, oy);
  Lines axes;
  axes.add(12, cy, w - 12, cy).add(cx, 12, cx, h - 12);
  // Ticks every 5 mm, longer every centimetre.
  const double step = 5 * MM;
  for (int k = -int(std::floor(cx / step)); cx + k * step < w - 14; k++) {
    if (k == 0) continue;
    const double t = k % 2 == 0 ? 6 : 3;
    const double x = cx + k * step;
    axes.add(x, cy - t, x, cy + t);
  }
  for (int k = -int(std::floor(cy / step)); cy + k * step < h - 14; k++) {
    if (k == 0 || cy + k * step < 14) continue;
    const double t = k % 2 == 0 ? 6 : 3;
    const double y = cy + k * step;
    axes.add(cx - t, y, cx + t, y);
  }
  // Arrow heads on the positive ends (right and up).
  axes.add(w - 12, cy, w - 21, cy - 5).add(w - 12, cy, w - 21, cy + 5);
  axes.add(cx, 12, cx - 5, 21).add(cx, 12, cx + 5, 21);
  axes.stroke(c, ink.strong, 1.4);
}

void drawIsometric(JsCanvas& c, double w, double h, const Inks& ink) {
  const double s = 24;
  const double rowH = s * std::sin(M_PI / 3);
  c.save();
  c.setFill(ink.dot);
  int row = 0;
  for (double y = 20; y < h - 4; y += rowH, row++)
    for (double x = row % 2 ? s / 2 + 8 : 8; x < w - 4; x += s) c.fillRect(x - 1.1, y - 1.1, 2.2, 2.2);
  c.restore();
}

void drawHexagons(JsCanvas& c, double w, double h, const Inks& ink) {
  const double r = 18;  // corner radius, flat-topped hexagons
  const double colW = r * 1.5, rowH = r * std::sqrt(3.0);
  Lines lines;
  int col = 0;
  for (double x = 0; x < w + r; col++, x += colW) {
    for (double y = col % 2 ? rowH / 2 : 0; y < h + rowH; y += rowH) {
      // Three edges per hexagon (upper-left, top, upper-right) tile the plane
      // without drawing shared edges twice.
      auto px = [&](double a) { return x + r * std::cos(a); };
      auto py = [&](double a) { return y + r * std::sin(a); };
      const double a0 = M_PI, a1 = (4 * M_PI) / 3, a2 = (5 * M_PI) / 3, a3 = 0;
      lines.add(px(a0), py(a0), px(a1), py(a1)).add(px(a1), py(a1), px(a2), py(a2)).add(px(a2), py(a2), px(a3), py(a3));
    }
  }
  lines.stroke(c, ink.line, 1);
}

void drawHandwriting(JsCanvas& c, double w, double h, const Inks& ink) {
  Lines solid, base, dashed, faint;
  for (const HandRow& r : handwritingRows(h)) {
    solid.h(r.top, 40, w - 40);
    dashed.h(r.mid, 40, w - 40);
    base.h(r.base, 40, w - 40);
    faint.h(r.desc, 40, w - 40);
  }
  faint.stroke(c, ink.faint, 1);
  solid.stroke(c, ink.line, 1);
  dashed.stroke(c, ink.line, 1, {7, 6});
  base.stroke(c, ink.strong, 1.3);
}

void drawCalligraphy(JsCanvas& c, double w, double h, const Inks& ink) {
  Lines faint, line, base, slant;
  const double angle = (55 * M_PI) / 180;  // slant lines, from the horizontal
  const double run = 1 / std::tan(angle);
  for (const CalliRow& r : calligraphyRows(h)) {
    faint.h(r.asc, 40, w - 40).h(r.desc, 40, w - 40);
    line.h(r.xh, 40, w - 40);
    base.h(r.base, 40, w - 40);
    const double span = r.desc - r.asc;
    for (double x = 40; x + span * run < w - 40; x += 30) slant.add(x, r.desc, x + span * run, r.asc);
  }
  slant.stroke(c, ink.faint, 0.9);
  faint.stroke(c, ink.faint, 1);
  line.stroke(c, ink.line, 1);
  base.stroke(c, ink.strong, 1.3);
}

void drawStoryboard(JsCanvas& c, double w, double h, const Inks& ink) {
  const double m = 44, g = 26;
  const int cols = w > h ? 3 : 2;
  const double fw = (w - 2 * m - (cols - 1) * g) / cols;
  const double fh = (fw * 9) / 16;
  const int capLines = 2;
  const double capGap = 24;
  const double cellH = fh + 14 + capLines * capGap;
  const int rows = std::max(1, int(std::floor((h - 2 * m + g) / (cellH + g))));
  const double top = (h - (rows * cellH + (rows - 1) * g)) / 2;
  Lines frames, captions;
  for (int r = 0; r < rows; r++) {
    for (int cc = 0; cc < cols; cc++) {
      const double x = m + cc * (fw + g);
      const double y = top + r * (cellH + g);
      frames.rect(x, y, fw, fh);
      for (int k = 1; k <= capLines; k++) captions.h(y + fh + 10 + k * capGap, x, x + fw);
    }
  }
  captions.stroke(c, ink.line, 1);
  frames.stroke(c, ink.strong, 1.4);
}

void drawDailyPlanner(JsCanvas& c, double w, double h, const Inks& ink) {
  const double m = 48;
  const double headerY = m + 40;
  label(c, "Date", m, m + 22, ink, 13, true);
  Lines strong, line, dashed;
  strong.h(headerY, m, w - m);
  // Schedule on the left, priorities / to-do / notes on the right.
  const double gap = 28;
  const double leftW = jsRound((w - 2 * m - gap) * 0.56);
  const double rx = m + leftW + gap;
  const double top = headerY + 34;
  const double bottom = h - m;
  label(c, "Schedule", m, top - 12, ink, 12, true);
  const int hours = 16;  // 6:00 to 21:00
  const double slot = (bottom - top) / hours;
  for (int i = 0; i <= hours; i++) {
    const double y = top + i * slot;
    line.h(y, m, m + leftW);
    if (i < hours) {
      dashed.h(y + slot / 2, m + 46, m + leftW);
      label(c, QString("%1:00").arg(6 + i), m + 2, y + 16, ink, 11);
    }
  }
  strong.v(m + 40, top, bottom);
  // Right column.
  label(c, "Top priorities", rx, top - 12, ink, 12, true);
  const double rowGap = 30;
  double y = top + rowGap;
  for (int i = 0; i < 3; i++, y += rowGap) {
    checkbox(line, rx, y);
    line.h(y, rx + 22, w - m);
  }
  y += 18;
  label(c, "To do", rx, y - 12, ink, 12, true);
  y += rowGap - 4;
  const double notesTop = top + (bottom - top) * 0.62;
  for (; y < notesTop - 10; y += rowGap) {
    checkbox(line, rx, y);
    line.h(y, rx + 22, w - m);
  }
  label(c, "Notes", rx, notesTop + 14, ink, 12, true);
  for (double ny = notesTop + 26 + rowGap; ny < bottom; ny += rowGap) line.h(ny, rx, w - m);
  dashed.stroke(c, ink.faint, 1, {4, 4});
  line.stroke(c, ink.line, 1);
  strong.stroke(c, ink.strong, 1.3);
}

void drawWeeklyPlanner(JsCanvas& c, double w, double h, const Inks& ink) {
  const double m = 40;
  label(c, "Week of", m, m + 20, ink, 13, true);
  Lines strong, line;
  const double headerY = m + 36;
  strong.h(headerY, m, w - m);
  const int cols = w > h ? 4 : 2;
  const int rows = 8 / cols;
  const double g = 16;
  const double top = headerY + 20;
  const double cw = (w - 2 * m - (cols - 1) * g) / cols;
  const double ch = (h - m - top - (rows - 1) * g) / rows;
  static const char* names[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday", "Notes"};
  for (int i = 0; i < 8; i++) {
    const double x = m + (i % cols) * (cw + g);
    const double y = top + (i / cols) * (ch + g);
    strong.rect(x, y, cw, ch);
    label(c, names[i], x + 10, y + 22, ink, 12, true);
    line.h(y + 32, x, x + cw);
    for (double ly = y + 32 + 28; ly < y + ch - 8; ly += 28) line.h(ly, x + 10, x + cw - 10);
  }
  line.stroke(c, ink.faint, 1);
  strong.stroke(c, ink.line, 1.2);
}

}  // namespace

bool isDarkPaper(const QString& color) {
  static const QRegularExpression hex(QStringLiteral("^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$"), QRegularExpression::CaseInsensitiveOption);
  const auto m = hex.match(color.trimmed());
  if (!m.hasMatch()) return false;
  const double r = m.captured(1).toInt(nullptr, 16) / 255.0, g = m.captured(2).toInt(nullptr, 16) / 255.0, b = m.captured(3).toInt(nullptr, 16) / 255.0;
  return 0.2126 * r + 0.7152 * g + 0.0722 * b < 0.45;
}

PaperLayout paperLayout(const QString& t, double w, double h) {
  auto lay = [&](std::vector<double> rules, double from, double to, bool hasMargin, double margin) { return PaperLayout{std::move(rules), from, to, hasMargin, margin}; };
  if (t == "ruled") return lay(ruledRows(34, h), 0, w, true, 90);
  if (t == "narrow-ruled") return lay(ruledRows(26, h), 0, w, true, 90);
  if (t == "wide-ruled") return lay(ruledRows(44, h), 0, w, true, 90);
  if (t == "legal") return lay(ruledRows(34, h), 0, w, true, 118);
  if (t == "checklist") return lay(ruledRows(40, h, 120), 72, w, true, 72);
  if (t == "two-column") return lay(twoColumnRules(h), 0, w, false, 0);
  if (t == "grid" || t == "dotted") return lay(ruledRows(24, h, 24, h), 0, w, false, 0);
  if (t == "cornell") {
    const double cue = jsRound(w * 0.28);
    const double summary = h - jsRound(h * 0.2);
    return lay(ruledRows(34, h, RULE_TOP, summary - 10), cue, w, true, cue);
  }
  if (t == "handwriting") {
    std::vector<double> r;
    for (const HandRow& row : handwritingRows(h)) r.push_back(row.base);
    return lay(r, 0, w, false, 0);
  }
  if (t == "calligraphy") {
    std::vector<double> r;
    for (const CalliRow& row : calligraphyRows(h)) r.push_back(row.base);
    return lay(r, 0, w, false, 0);
  }
  return lay({}, 0, w, false, 0);
}

bool drawPaper(JsCanvas& c, const QString& t, double w, double h, const QString& paper) {
  c.setFill(paper);
  c.fillRect(0, 0, w, h);
  const Inks ink = inks(paper);
  Lines L, M;
  if (t == "ruled" || t == "narrow-ruled" || t == "wide-ruled") {
    for (double y : paperLayout(t, w, h).rules) L.h(y, 0, w);
    L.stroke(c, ink.line);
    M.v(90, 0, h).stroke(c, ink.margin);
  } else if (t == "legal") {
    for (double y : paperLayout(t, w, h).rules) L.h(y, 0, w);
    L.stroke(c, ink.line);
    M.v(112, 0, h).v(118, 0, h).stroke(c, ink.margin);
  } else if (t == "checklist") {
    for (double y : paperLayout(t, w, h).rules) {
      L.h(y, 72, w - 40);
      checkbox(L, 44, y, 15);
    }
    L.stroke(c, ink.line);
  } else if (t == "two-column") {
    const double mid = jsRound(w / 2);
    L.h(110, 40, w - 40);
    for (double y : twoColumnRules(h)) L.h(y, 40, mid - 14).h(y, mid + 14, w - 40);
    L.stroke(c, ink.line);
    M.v(mid, 70, h - 30).stroke(c, ink.strong, 1.2);
  } else if (t == "grid") {
    for (double y = 24; y < h; y += 24) L.h(y, 0, w);
    for (double x = 24; x < w; x += 24) L.v(x, 0, h);
    L.stroke(c, ink.line);
  } else if (t == "dotted") {
    c.save();
    c.setFill(ink.dot);
    for (double y = 24; y < h; y += 24)
      for (double x = 24; x < w; x += 24) c.fillRect(x - 1, y - 1, 2.2, 2.2);
    c.restore();
  } else if (t == "cornell") {
    const double cue = jsRound(w * 0.28);
    const double summary = h - jsRound(h * 0.2);
    for (double y : paperLayout(t, w, h).rules) L.h(y, cue, w);
    L.stroke(c, ink.line);
    M.v(cue, 70, summary).h(70, 0, w).h(summary, 0, w).stroke(c, ink.margin);
  } else if (t == "graph") {
    drawGraph(c, w, h, ink);
  } else if (t == "coordinate") {
    drawCoordinate(c, w, h, ink);
  } else if (t == "isometric") {
    drawIsometric(c, w, h, ink);
  } else if (t == "hexagon") {
    drawHexagons(c, w, h, ink);
  } else if (t == "handwriting") {
    drawHandwriting(c, w, h, ink);
  } else if (t == "calligraphy") {
    drawCalligraphy(c, w, h, ink);
  } else if (t == "storyboard") {
    drawStoryboard(c, w, h, ink);
  } else if (t == "planner-day") {
    drawDailyPlanner(c, w, h, ink);
  } else if (t == "planner-week") {
    drawWeeklyPlanner(c, w, h, ink);
  } else if (isCoverTemplate(t)) {
    drawCoverDesign(c, t, w, h, paper);
  } else if (t != "blank") {
    return false;  // daftar pages
  }
  return true;
}

}  // namespace draw
