#include "Table.h"

#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

#include "../draw/Elements.h"
#include "Geometry.h"

namespace ed {

namespace {
constexpr QChar kCellBreak = QChar(0x2028);

std::vector<double> normalized(std::vector<double> v) {
  double sum = 0;
  for (double d : v) sum += std::max(0.0, d);
  for (double& d : v) d = sum > 0 ? std::max(0.0, d) / sum : 1.0 / v.size();
  return v;
}
QJsonArray toArray(const std::vector<double>& v) {
  QJsonArray a;
  for (double d : v) a.append(std::round(d * 1e5) / 1e5);
  return a;
}
}  // namespace

QString TableData::cell(int r, int c) const {
  return r >= 0 && r < int(cells.size()) && c >= 0 && c < cells[r].size() ? cells[r][c] : QString();
}

void TableData::setCell(int r, int c, const QString& text) {
  if (r < 0 || r >= rowCount() || c < 0 || c >= colCount()) return;
  while (int(cells.size()) <= r) cells.emplace_back();
  while (cells[r].size() <= c) cells[r] << QString();
  cells[r][c] = text;
}

TableData tableData(const El& el) {
  const draw::TableGrid l = draw::tableGrid(el.toJson());
  TableData t;
  t.header = l.header;
  for (int c = 0; c < l.cols; ++c) t.cols.push_back(el.w > 0 ? (l.xs[c + 1] - l.xs[c]) / el.w : 1.0 / l.cols);
  for (int r = 0; r < l.rows; ++r) t.rows.push_back(el.h > 0 ? (l.ys[r + 1] - l.ys[r]) / el.h : 1.0 / l.rows);
  for (int r = 0; r < l.rows; ++r) {
    QStringList row;
    for (int c = 0; c < l.cols; ++c) row << l.cell(r, c);
    t.cells.push_back(row);
  }
  return t;
}

void setTableData(El& el, const TableData& t) {
  QStringList lines;
  for (int r = 0; r < t.rowCount(); ++r) {
    QStringList row;
    for (int c = 0; c < t.colCount(); ++c) row << QString(t.cell(r, c)).replace('\t', ' ').replace('\n', kCellBreak);
    lines << row.join('\t');
  }
  el.text = lines.join('\n');
  el.extra["cols"] = toArray(normalized(t.cols));
  el.extra["rows"] = toArray(normalized(t.rows));
  el.extra["header"] = t.header;
  el.cachedBounds.reset();
  el.cachedFrame.reset();
}

std::optional<std::pair<int, int>> tableCellAt(const El& el, double x, double y) {
  if (el.kind != Kind::Table || el.rot() != 0) return std::nullopt;
  const draw::TableGrid l = draw::tableGrid(el.toJson());
  if (l.rows < 1 || l.cols < 1 || x < l.xs.front() || x > l.xs.back() || y < l.ys.front() || y > l.ys.back()) return std::nullopt;
  int r = 0, c = 0;
  while (r + 1 < l.rows && y > l.ys[r + 1]) ++r;
  while (c + 1 < l.cols && x > l.xs[c + 1]) ++c;
  return std::make_pair(r, c);
}

Rect tableCellRect(const El& el, int row, int col) {
  const draw::TableGrid l = draw::tableGrid(el.toJson());
  if (row < 0 || col < 0 || row >= l.rows || col >= l.cols) return {};
  return {l.xs[col], l.ys[row], l.xs[col + 1] - l.xs[col], l.ys[row + 1] - l.ys[row]};
}

void fitTableRows(El& el) {
  TableData t = tableData(el);
  if (t.rowCount() < 1 || t.colCount() < 1) return;
  const double fs = el.fontSize.value_or(18), pad = draw::tableCellPad(fs);
  std::vector<double> heights;
  for (double f : t.rows) heights.push_back(f * el.h);
  bool grew = false;
  for (int r = 0; r < t.rowCount(); ++r)
    for (int c = 0; c < t.colCount(); ++c) {
      const QString text = t.cell(r, c);
      if (text.trimmed().isEmpty()) continue;
      draw::TextStyle st;
      st.font = el.font.value_or(QString());
      st.bold = t.header && r == 0;
      const double need = draw::textHeight(text, fs, std::max(4.0, t.cols[c] * el.w - 2 * pad), st) + pad;
      if (need > heights[r] + 0.5) heights[r] = need, grew = true;
    }
  if (!grew) return;
  double total = 0;
  for (double h : heights) total += h;
  el.h = total;
  t.rows = heights;
  setTableData(el, t);
}

bool changeTable(El& el, const QString& op, int index) {
  TableData t = tableData(el);
  const int rows = t.rowCount(), cols = t.colCount();
  if (op == "header") {
    t.header = !t.header;
  } else if (op == "addRow") {
    const int at = index < 0 || index >= rows ? rows : index + 1;
    const double share = t.rows.empty() ? 1 : t.rows[std::clamp(at - 1, 0, rows - 1)];
    // The table grows by the new row; the others keep their height.
    const double h = share * el.h;
    for (double& f : t.rows) f = f * el.h;
    t.rows.insert(t.rows.begin() + at, h);
    t.cells.insert(t.cells.begin() + std::min<int>(at, int(t.cells.size())), QStringList(QVector<QString>(cols).toList()));
    el.h += h;
  } else if (op == "addCol") {
    const int at = index < 0 || index >= cols ? cols : index + 1;
    // The new column takes its share from the others (the width stays).
    const double share = 1.0 / (cols + 1);
    for (double& f : t.cols) f *= 1 - share;
    t.cols.insert(t.cols.begin() + at, share);
    for (QStringList& row : t.cells) row.insert(std::min<int>(at, int(row.size())), QString());
  } else if (op == "removeRow") {
    if (rows <= 1) return false;
    const int at = index < 0 || index >= rows ? rows - 1 : index;
    const double h = t.rows[at] * el.h;
    for (double& f : t.rows) f = f * el.h;
    t.rows.erase(t.rows.begin() + at);
    if (at < int(t.cells.size())) t.cells.erase(t.cells.begin() + at);
    el.h = std::max(8.0, el.h - h);
  } else if (op == "removeCol") {
    if (cols <= 1) return false;
    const int at = index < 0 || index >= cols ? cols - 1 : index;
    t.cols.erase(t.cols.begin() + at);
    for (QStringList& row : t.cells)
      if (at < row.size()) row.removeAt(at);
  } else {
    return false;
  }
  setTableData(el, t);
  return true;
}

bool computeAverages(El& el) {
  if (!el.extra.contains("average")) return false;
  const bool rtl = el.extra.value("average").toObject().value("rtl").toBool();
  TableData t = tableData(el);
  const int cols = t.colCount();
  if (cols < 4) return false;
  // Left to right: N°, name, marks…, average; right to left the other way round.
  const int avgCol = rtl ? 0 : cols - 1;
  const int first = rtl ? 1 : 2, last = rtl ? cols - 3 : cols - 2;
  static const QRegularExpression number(QStringLiteral("^\\s*(\\d+(?:[.,]\\d+)?)\\s*(?:/\\s*\\d+)?\\s*$"));
  bool changed = false;
  for (int r = t.header ? 1 : 0; r < t.rowCount(); ++r) {
    double sum = 0;
    int n = 0;
    for (int c = first; c <= last; ++c) {
      const auto m = number.match(t.cell(r, c));
      if (!m.hasMatch()) continue;
      sum += m.captured(1).replace(',', '.').toDouble();
      ++n;
    }
    QString avg;
    if (n > 0) {
      const double v = std::round(sum / n * 100) / 100;
      avg = QString::number(v, 'f', v == std::floor(v) ? 0 : 2);
      if (avg.contains('.')) {
        while (avg.endsWith('0')) avg.chop(1);
        avg.replace('.', ',');
      }
    }
    if (t.cell(r, avgCol) != avg) t.setCell(r, avgCol, avg), changed = true;
  }
  if (changed) setTableData(el, t);
  return changed;
}

// ------------------------------------------------------------ drawn grids

namespace {
struct Seg {
  bool horiz;
  double pos, from, to;  ///< y (or x), and its extent along the line
  QString id;
  double len() const { return to - from; }
};

/// A polyline's straight, level or upright stretch from end to end, if it is one.
std::optional<Seg> straight(const std::vector<P2>& pts, const QString& id) {
  if (pts.size() < 2) return std::nullopt;
  const P2 a = pts.front(), b = pts.back();
  const double dx = b[0] - a[0], dy = b[1] - a[1], len = std::hypot(dx, dy);
  if (len < 24) return std::nullopt;
  double dev = 0;
  for (const P2& p : pts) dev = std::max(dev, distToSegment(p[0], p[1], a[0], a[1], b[0], b[1]));
  if (dev > std::max(5.0, len * 0.07)) return std::nullopt;
  if (std::abs(dy) <= std::abs(dx) * 0.3) return Seg{true, (a[1] + b[1]) / 2, std::min(a[0], b[0]), std::max(a[0], b[0]), id};
  if (std::abs(dx) <= std::abs(dy) * 0.3) return Seg{false, (a[0] + b[0]) / 2, std::min(a[1], b[1]), std::max(a[1], b[1]), id};
  return std::nullopt;
}

/// A box drawn in one stroke: its four sides.
std::vector<Seg> boxSides(const std::vector<P2>& pts, const QString& id) {
  if (pts.size() < 8) return {};
  const Rect r = rectOfPts(pts);
  if (r.w < 24 || r.h < 24) return {};
  const double gap = std::hypot(pts.front()[0] - pts.back()[0], pts.front()[1] - pts.back()[1]);
  if (gap > std::max(30.0, std::min(r.w, r.h) * 0.35)) return {};
  const double tol = std::max(6.0, std::min(r.w, r.h) * 0.12);
  int onEdge = 0;
  for (const P2& p : pts) {
    const double d = std::min({std::abs(p[0] - r.x), std::abs(p[0] - r.x - r.w), std::abs(p[1] - r.y), std::abs(p[1] - r.y - r.h)});
    if (d <= tol) ++onEdge;
  }
  if (onEdge < int(pts.size() * 0.85)) return {};
  return {{true, r.y, r.x, r.x + r.w, id}, {true, r.y + r.h, r.x, r.x + r.w, id}, {false, r.x, r.y, r.y + r.h, id}, {false, r.x + r.w, r.y, r.y + r.h, id}};
}

/// Edges close together (a hand-drawn line twice, a wobble) become one.
std::vector<double> cluster(std::vector<double> v, double tol) {
  std::sort(v.begin(), v.end());
  std::vector<double> out;
  std::vector<double> group;
  auto flush = [&] {
    if (group.empty()) return;
    double s = 0;
    for (double d : group) s += d;
    out.push_back(s / group.size());
    group.clear();
  };
  for (double d : v) {
    if (!group.empty() && d - group.back() > tol) flush();
    group.push_back(d);
  }
  flush();
  return out;
}
}  // namespace

std::optional<DrawnGrid> findDrawnGrid(const std::vector<const El*>& els) {
  std::vector<Seg> segs;
  QMap<QString, const El*> byId;
  for (const El* el : els) {
    byId[el->id] = el;
    if (el->kind == Kind::Stroke && el->tool != "highlighter") {
      std::vector<P2> pts;
      for (const Pt& p : el->points) pts.push_back({p[0], p[1]});
      if (auto s = straight(pts, el->id)) segs.push_back(*s);
      else for (const Seg& s : boxSides(pts, el->id)) segs.push_back(s);
    } else if (el->kind == Kind::Shape && el->shape.kind != "ellipse") {
      for (std::vector<P2> part : shapeParts(el->shape)) {
        if (isClosedShape(el->shape) && !part.empty()) part.push_back(part.front());
        for (size_t i = 0; i + 1 < part.size(); ++i)
          if (auto s = straight({part[i], part[i + 1]}, el->id)) segs.push_back(*s);
      }
    }
  }
  // The grid's extent, then only lines across most of it (not a digit's
  // stroke or an underline in a cell), then its extent again.
  auto frameOf = [](const std::vector<Seg>& ss) {
    double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    for (const Seg& s : ss) {
      if (s.horiz) x0 = std::min(x0, s.from), x1 = std::max(x1, s.to), y0 = std::min(y0, s.pos), y1 = std::max(y1, s.pos);
      else y0 = std::min(y0, s.from), y1 = std::max(y1, s.to), x0 = std::min(x0, s.pos), x1 = std::max(x1, s.pos);
    }
    return Rect{x0, y0, x1 - x0, y1 - y0};
  };
  for (int pass = 0; pass < 2; ++pass) {
    if (segs.empty()) return std::nullopt;
    const Rect f = frameOf(segs);
    std::vector<Seg> kept;
    for (const Seg& s : segs)
      if (s.len() >= (s.horiz ? f.w : f.h) * 0.45) kept.push_back(s);
    segs = kept;
  }
  int hs = 0, vs = 0;
  for (const Seg& s : segs) (s.horiz ? hs : vs)++;
  if (hs < 2 || vs < 2) return std::nullopt;
  const Rect f = frameOf(segs);
  if (f.w < 40 || f.h < 30) return std::nullopt;
  std::vector<double> xs{f.x, f.x + f.w}, ys{f.y, f.y + f.h};
  for (const Seg& s : segs) (s.horiz ? ys : xs).push_back(s.pos);
  DrawnGrid g;
  g.xs = cluster(xs, std::max(12.0, f.w * 0.06));
  g.ys = cluster(ys, std::max(10.0, f.h * 0.06));
  if (g.xs.size() < 2 || g.ys.size() < 2 || (g.xs.size() - 1) * (g.ys.size() - 1) < 2) return std::nullopt;
  g.xs.front() = f.x, g.xs.back() = f.x + f.w;
  g.ys.front() = f.y, g.ys.back() = f.y + f.h;
  g.frame = f;
  QMap<QString, int> colors;
  double widthSum = 0;
  for (const Seg& s : segs) g.lineIds.insert(s.id);
  for (const QString& id : g.lineIds) {
    colors[byId[id]->color.toLower()]++;
    widthSum += byId[id]->width;
  }
  int best = -1;
  for (auto it = colors.begin(); it != colors.end(); ++it)
    if (it.value() > best) best = it.value(), g.color = it.key();
  g.width = std::clamp(widthSum / std::max<qsizetype>(1, g.lineIds.size()) * 0.6, 1.0, 3.0);
  return g;
}

}  // namespace ed
