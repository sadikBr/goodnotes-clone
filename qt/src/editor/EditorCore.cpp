// The editor for one notebook (was qt/js/editorCore.ts); see EditorCore.h.
#include "EditorCore.h"
#include "Table.h"
#include "Gestures.h"
#include "../draw/Render.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "../Core.h"
#include "../Canvas.h"
#include "../draw/Render.h"
#include "../draw/Elements.h"
#include "../draw/Fonts.h"
#include "../draw/Papers.h"
#include "../draw/Strokes.h"
#include "TextFormat.h"
#include "../Ink.h"



namespace ed {

namespace {
bool isTap(const std::vector<Pt>& pts, double within) {
  double x0 = pts[0][0], x1 = x0, y0 = pts[0][1], y1 = y0;
  for (const Pt& p : pts) x0 = std::min(x0, p[0]), x1 = std::max(x1, p[0]), y0 = std::min(y0, p[1]), y1 = std::max(y1, p[1]);
  return x1 - x0 < within && y1 - y0 < within;
}
/// The parts of a stroke inside [0, w] × [0, h], cut where it crosses the edge.
std::vector<std::vector<Pt>> clipToRect(const std::vector<Pt>& pts, double w, double h) {
  auto inside = [&](const Pt& p) { return p[0] >= 0 && p[1] >= 0 && p[0] <= w && p[1] <= h; };
  std::vector<std::vector<Pt>> out;
  if (pts.empty()) return out;
  if (std::all_of(pts.begin(), pts.end(), inside)) return {pts};
  std::vector<Pt> cur;
  if (inside(pts[0])) cur.push_back(pts[0]);
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const Pt a = pts[i], b = pts[i + 1];
    // Liang–Barsky: the part of segment a→b inside the rectangle.
    double t0 = 0, t1 = 1;
    const double dx = b[0] - a[0], dy = b[1] - a[1];
    bool visible = true;
    for (const auto& [pp, q] : {std::pair{-dx, a[0]}, std::pair{dx, w - a[0]}, std::pair{-dy, a[1]}, std::pair{dy, h - a[1]}}) {
      if (std::abs(pp) < 1e-12) {
        if (q < 0) visible = false;
      } else {
        const double t = q / pp;
        if (pp < 0) t0 = std::max(t0, t);
        else t1 = std::min(t1, t);
      }
    }
    if (!visible || t0 > t1) {
      if (cur.size() > 1) out.push_back(cur);
      cur.clear();
      continue;
    }
    auto at = [&](double t) { return Pt{a[0] + dx * t, a[1] + dy * t, a[2] + (b[2] - a[2]) * t}; };
    if (t0 > 0) {
      if (cur.size() > 1) out.push_back(cur);
      cur = {at(t0)};
    } else if (cur.empty()) {
      cur.push_back(a);
    }
    cur.push_back(t1 < 1 ? at(t1) : b);
    if (t1 < 1) {
      out.push_back(cur);
      cur.clear();
    }
  }
  if (cur.size() > 1) out.push_back(cur);
  // Drop crumbs: pieces shorter than a dot.
  out.erase(std::remove_if(out.begin(), out.end(), [](const std::vector<Pt>& v) {
              double len = 0;
              for (size_t i = 1; i < v.size(); ++i) len += std::hypot(v[i][0] - v[i - 1][0], v[i][1] - v[i - 1][1]);
              return len < 0.5;
            }), out.end());
  return out;
}
/// Scribbling with these is shading or painting, never a scratch-out.
bool scratchable(const QString& style) { return style != "marker" && style != "watercolor" && style != "neon" && style != "tape"; }
/// Pens whose look a held shape keeps (it is drawn with the pen, not as a plain shape).
bool keepsPen(const QString& style) { return style == "marker" || style == "watercolor" || style == "neon" || style == "construction"; }
/// Points every `step` along a polyline (its corners kept).
std::vector<P2> densify(const std::vector<P2>& pts, double step) {
  std::vector<P2> out;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const P2 a = pts[i], b = pts[i + 1];
    const int n = std::max(1, int(std::ceil(std::hypot(b[0] - a[0], b[1] - a[1]) / step)));
    for (int k = 0; k < n; ++k) out.push_back({a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n});
  }
  if (!pts.empty()) out.push_back(pts.back());
  return out;
}
/// A shape as the lines a pen would draw it with (an arrow: its shaft and its head).
std::vector<std::vector<P2>> shapeLines(const Shape& s, double width) {
  std::vector<std::vector<P2>> out;
  if (s.kind == "ellipse") {
    std::vector<P2> ring;
    for (int i = 0; i <= 72; ++i) ring.push_back({s.cx + s.rx * std::cos(i * M_PI / 36), s.cy + s.ry * std::sin(i * M_PI / 36)});
    out.push_back(ring);
  } else if (s.kind == "arrow" && s.points.size() >= 2) {
    const P2 a = s.points.front(), b = s.points.back();
    out.push_back({a, b});
    const double ang = std::atan2(b[1] - a[1], b[0] - a[0]), len = std::max(14.0, width * 4.5);
    out.push_back({{b[0] - len * std::cos(ang - 0.45), b[1] - len * std::sin(ang - 0.45)}, b, {b[0] - len * std::cos(ang + 0.45), b[1] - len * std::sin(ang + 0.45)}});
  } else {
    for (std::vector<P2> part : shapeParts(s)) {
      if (part.size() < 2) continue;
      if (isClosedShape(s) && s.breaks.empty()) part.push_back(part.front());
      out.push_back(part);
    }
  }
  for (auto& line : out) line = densify(line, std::max(2.0, width * 0.8));
  return out;
}
std::vector<Pt> straightTape(const std::vector<Pt>& pts, double width) {
  const Pt a = pts.front(), b = pts.back();
  const double len = std::hypot(b[0] - a[0], b[1] - a[1]);
  const int n = std::max(1, int(std::ceil(len / std::max(2.0, width / 2))));
  std::vector<Pt> out;
  for (int i = 0; i <= n; ++i) {
    const double t = double(i) / n;
    out.push_back({a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, 0.5});
  }
  return out;
}
}  // namespace

namespace {

constexpr int HOLD_MS = 550;
constexpr size_t MAX_UNDO = 100;

QJsonValue native(const QString& cmd, const QJsonObject& args) {
  QString err;
  const QJsonValue v = InkleafCore::instance()->callJson(cmd, args, &err);
  if (!err.isEmpty()) qWarning().noquote() << "editor:" << cmd << err;
  return v;
}

QJsonArray p2Json(const std::vector<P2>& pts) {
  QJsonArray a;
  for (const P2& p : pts) a.append(QJsonArray{p[0], p[1]});
  return a;
}

// Shape and symbol recognition: the Rust core (core/src/recognize).
std::optional<Shape> shapeOf(const QJsonValue& v) {
  if (!v.isObject()) return std::nullopt;
  return Shape::fromJson(v.toObject());
}
std::optional<Shape> recognizeShape(const std::vector<P2>& pts) { return shapeOf(native("recognizeShape", {{"points", p2Json(pts)}})); }
std::optional<Shape> beautify(const std::vector<P2>& pts) { return shapeOf(native("beautify", {{"points", p2Json(pts)}})); }
std::optional<QString> matchSymbol(const std::vector<std::vector<P2>>& strokes) {
  QJsonArray a;
  for (const auto& s : strokes) a.append(p2Json(s));
  const QJsonValue v = native("matchSymbol", {{"strokes", a}});
  if (!v.isObject()) return std::nullopt;
  return v.toObject()["name"].toString();
}
Shape symbolShape(const QString& name, const Rect& b) {
  return shapeOf(native("symbolShape", {{"name", name}, {"x", b.x}, {"y", b.y}, {"w", b.w}, {"h", b.h}})).value_or(Shape{});
}

std::vector<P2> flat(const std::vector<Pt>& pts) {
  std::vector<P2> out;
  out.reserve(pts.size());
  for (const Pt& p : pts) out.push_back({p[0], p[1]});
  return out;
}

const Rect& boxOfEl(const El& el) {
  if (!el.cachedBounds) el.cachedBounds = bounds(el);
  return *el.cachedBounds;
}
/// The selection frame around an element: its box, except unrotated text,
/// whose frame fits its letters (a few units around them), so it neither
/// leaves empty room nor cuts through tall letters and marks.
const Rect& frameOfEl(const El& el) {
  if (el.cachedFrame) return *el.cachedFrame;
  const Rect& box = boxOfEl(el);
  el.cachedFrame = box;
  if (el.kind == Kind::Text && !el.rot() && !el.text.trimmed().isEmpty()) {
    const draw::TextStyle ts{el.font.value_or(QString()), el.bold.value_or(false), el.italic.value_or(false), el.align.value_or(QString())};
    const QRectF ink = draw::textInk(el.text, el.x, el.y, el.w, el.fontSize.value_or(20), ts);
    if (!ink.isEmpty()) {
      const double pad = 4;
      el.cachedFrame = Rect{ink.x() - pad, ink.y() - pad, ink.width() + 2 * pad, ink.height() + 2 * pad};
    }
  }
  return *el.cachedFrame;
}
bool near(const Rect& r, double x, double y, double pad) { return x >= r.x - pad && x <= r.x + r.w + pad && y >= r.y - pad && y <= r.y + r.h + pad; }
bool overlaps(const Rect& a, const Rect& b) { return a.x <= b.x + b.w && b.x <= a.x + a.w && a.y <= b.y + b.h && b.y <= a.y + a.h; }
/// rectOfPts as the script had it (Math.min over the points; no padding).
Rect rectOf(const std::vector<P2>& pts) {
  if (pts.empty()) return {INFINITY, INFINITY, -INFINITY, -INFINITY};
  double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
  for (const P2& p : pts) {
    x0 = std::min(x0, p[0]);
    y0 = std::min(y0, p[1]);
    x1 = std::max(x1, p[0]);
    y1 = std::max(y1, p[1]);
  }
  return {x0, y0, x1 - x0, y1 - y0};
}
Rect pad(const Rect& r, double p) { return {r.x - p, r.y - p, r.w + 2 * p, r.h + 2 * p}; }
bool hasLetter(const QString& s) {
  for (QChar c : s)
    if (c.isLetter()) return true;
  return false;
}
QString trimEnd(const QString& s) {
  // JavaScript's \s (Unicode spaces, line ends, BOM).
  static const QRegularExpression re(QStringLiteral("[\\s\\x{feff}\\x{a0}]+$"), QRegularExpression::UseUnicodePropertiesOption);
  QString t = s;
  return t.remove(re);
}
QString trimmed(const QString& s) {
  static const QRegularExpression re(QStringLiteral("^[\\s\\x{feff}\\x{a0}]+|[\\s\\x{feff}\\x{a0}]+$"), QRegularExpression::UseUnicodePropertiesOption);
  QString t = s;
  return t.remove(re);
}
ElPtr copyOf(const El& e) { return makeEl(e); }

}  // namespace

double EditorCore::nextZOf(const Els& els) {
  double m = -1;
  for (const ElPtr& e : els) m = std::max(m, e->z);
  return m + 1;
}

const Rect& EditorCore::boxOf(const El& el) { return boxOfEl(el); }

void EditorCore::setElementsJson(const QJsonArray& list) {
  std::unordered_map<QString, ElPtr> old;
  for (const ElPtr& e : elements()) old.emplace(e->id, e);
  Els next;
  for (const QJsonValue& v : list) {
    El e = El::fromJson(v.toObject());
    auto was = old.find(e.id);
    if (was != old.end() && was->second->toJson() == e.toJson()) next.push_back(was->second);
    else next.push_back(makeEl(std::move(e)));
  }
  elements() = std::move(next);
}

EditorCore::EditorCore(const QJsonArray& pageList) {
  for (const auto& v : pageList) pages.push_back({v.toObject(), std::nullopt, false});
  m_holdTimer.setSingleShot(true);
  m_holdTimer.setInterval(HOLD_MS);
  m_stillTimer.setSingleShot(true);
  m_stillTimer.setInterval(HOLD_MS + 150);
  QObject::connect(&m_holdTimer, &QTimer::timeout, [this] {
    holdFired();
    if (onTimer) onTimer();
  });
  QObject::connect(&m_stillTimer, &QTimer::timeout, [this] {
    stillFired();
    if (onTimer) onTimer();
  });
}

EditorCore::~EditorCore() = default;

// ------------------------------------------------------------ pages

Els& EditorCore::load(int i) {
  static Els none;
  if (i < 0 || i >= int(pages.size())) {
    none.clear();
    return none;
  }
  PageState& p = pages[size_t(i)];
  if (!p.elements) {
    Els els;
    for (El& e : fromStored(native("elements", {{"pageId", p.meta["id"].toString()}}).toArray())) els.push_back(makeEl(std::move(e)));
    p.elements = std::move(els);
  }
  return *p.elements;
}

void EditorCore::activate(int i) {
  if (i == active || i < 0 || i >= int(pages.size())) return;
  finishTextEdit();
  m_selection.clear();
  active = i;
  load(i);
  touchOverlay();
  ++selectionRev;
}

void EditorCore::forgetAll() {
  finishTextEdit();
  m_selection.clear();
  for (PageState& p : pages)
    if (!p.dirty) p.elements.reset();
  m_undo.clear();
  m_redo.clear();
  ++rev;
  ++selectionRev;
}

void EditorCore::setPages(const QJsonArray& list) {
  // The history names pages by place: it follows them by id (a step on a page
  // that's gone is dropped).
  std::vector<QString> oldIds;
  for (const PageState& p : pages) oldIds.push_back(p.meta["id"].toString());
  QHash<QString, int> newIndex;
  for (int k = 0; k < list.size(); ++k) newIndex.insert(list[k].toObject()["id"].toString(), k);
  for (auto* steps : {&m_undo, &m_redo}) {
    std::vector<Step> kept;
    for (Step& st : *steps) {
      if (st.page < 0 || st.page >= int(oldIds.size())) continue;
      const auto it = newIndex.constFind(oldIds[size_t(st.page)]);
      if (it == newIndex.constEnd()) continue;
      st.page = *it;
      kept.push_back(std::move(st));
    }
    *steps = std::move(kept);
  }
  std::unordered_map<QString, PageState> old;
  for (PageState& p : pages) old.emplace(p.meta["id"].toString(), std::move(p));
  const QString activeId = active >= 0 && active < int(old.size() + pages.size()) && !pages.empty() ? pages[size_t(std::min(active, int(pages.size()) - 1))].meta["id"].toString() : QString();
  std::vector<PageState> next;
  for (const auto& v : list) {
    const QJsonObject meta = v.toObject();
    auto was = old.find(meta["id"].toString());
    if (was != old.end()) {
      PageState s = std::move(was->second);
      s.meta = meta;
      next.push_back(std::move(s));
    } else {
      next.push_back({meta, std::nullopt, false});
    }
  }
  pages = std::move(next);
  int idx = -1;
  for (int k = 0; k < int(pages.size()); ++k)
    if (pages[size_t(k)].meta["id"].toString() == activeId) idx = k;
  active = idx >= 0 ? idx : std::max(0, std::min(active, int(pages.size()) - 1));
  ++rev;
}

// ------------------------------------------------------------ history and saving

void EditorCore::snapshot() {
  ++newChanges;
  m_undo.push_back({active, elements()});
  if (m_undo.size() > MAX_UNDO) m_undo.erase(m_undo.begin());
  m_redo.clear();
}

void EditorCore::changed() {
  pages[size_t(active)].dirty = true;
  ++rev;
  touchOverlay();
  ++selectionRev;
}

void EditorCore::undo() {
  finishTextEdit();
  if (m_undo.empty()) return;
  Step prev = std::move(m_undo.back());
  m_undo.pop_back();
  activate(prev.page);
  m_redo.push_back({active, elements()});
  elements() = std::move(prev.els);
  m_selection.clear();
  changed();
}

void EditorCore::redo() {
  finishTextEdit();
  if (m_redo.empty()) return;
  Step next = std::move(m_redo.back());
  m_redo.pop_back();
  activate(next.page);
  m_undo.push_back({active, elements()});
  elements() = std::move(next.els);
  m_selection.clear();
  changed();
}

void EditorCore::addElement(ElPtr el) {
  snapshot();
  elements().push_back(std::move(el));
  changed();
}

bool EditorCore::dirty() const {
  return std::any_of(pages.begin(), pages.end(), [](const PageState& p) { return p.dirty; });
}

std::vector<int> EditorCore::takeDirty() {
  std::vector<int> out;
  for (int i = 0; i < int(pages.size()); ++i) {
    PageState& p = pages[size_t(i)];
    if (p.dirty && p.elements) {
      p.dirty = false;
      out.push_back(i);
    }
  }
  return out;
}

void EditorCore::markDirty(int i) {
  if (i >= 0 && i < int(pages.size())) pages[size_t(i)].dirty = true;
}

// ------------------------------------------------------------ pointer

QString EditorCore::down(int pi, double x, double y, double p, double t, const QString& tool_, bool shift) {
  m_shift = shift;
  if (m_g.type != G::None) return {};
  if (pi != active) activate(pi);
  finishTextEdit();
  m_cursor = P2{x, y};
  const QString& tl = tool_;
  if (tl == "pen" || tl == "highlighter" || tl == "shape") {
    // Starting next to the instrument: the stroke runs along its edge.
    if (auto edge = m_ruler.edgeNear({x, y}, 26 / zoom)) {
      const double offset = widths.value(tl) / 2 + 0.5 / zoom;
      const P2 q = projectOnto(*edge, {x, y}, offset);
      m_g = Gesture{};
      m_g.type = G::Draw;
      m_g.serial = ++m_serial;
      m_g.tool = tl;
      m_g.points = {{q[0], q[1], p}};
      m_g.times = {t};
      m_g.ruler = Guided{*edge, offset};
      return "ruler";
    }
    if (tl == "shape" && shapeKind != "auto") {
      m_g = Gesture{};
      m_g.type = G::Shape;
      m_g.serial = ++m_serial;
      m_g.start = {x, y};
      return {};
    }
    m_g = Gesture{};
    m_g.type = G::Draw;
    m_g.serial = ++m_serial;
    m_g.tool = tl;
    m_g.points = {{x, y, p}};
    m_g.times = {t};
    armHold();
    return "draw";
  }
  if (tl == "eraser") {
    m_g = Gesture{};
    m_g.type = G::Erase;
    m_g.serial = ++m_serial;
    eraseAlong({x, y});
    return {};
  }
  if (tl == "lasso") {
    const ElPtr single = singleSegment();
    const QString handle = single ? handleAt(*single, x, y, 10 / zoom) : QString();
    if (single && !handle.isEmpty()) {
      m_g = Gesture{};
      m_g.type = G::Handle;
      m_g.serial = ++m_serial;
      m_g.handle = handle;
      m_g.original = {single};
      m_g.preview = single;
      m_hidden.insert(single->id);
      ++rev;
    } else {
      const auto content = selectionBounds();
      const bool onContent = content && rectContains(*content, x, y);
      const auto tbox = single || onContent ? std::nullopt : transformBox();
      const QString gh = tbox ? groupHandleAt(*tbox, x, y, zoom) : QString();
      if (tbox && !gh.isEmpty()) {
        m_g = Gesture{};
        m_g.type = G::Transform;
        m_g.serial = ++m_serial;
        m_g.handle = gh;
        m_g.start = {x, y};
        m_g.box = *tbox;
        for (const ElPtr& el : elements())
          if (m_selection.contains(el->id)) m_g.original.push_back(el);
        // Text is shown as it will be (re-wrapped, resized) while dragging,
        // not as its old picture stretched: hidden on the page meanwhile.
        bool anyText = false;
        for (const ElPtr& el : m_g.original)
          if (el->isTextish()) m_hidden.insert(el->id), anyText = true;
        // Shown as they are until the pointer moves (hidden on the page already).
        for (const ElPtr& el : m_g.original)
          if (el->isTextish()) m_g.previews.push_back(el);
        if (anyText) ++rev;
      } else {
        const auto sel = selectionBounds();
        if (sel && rectContains({sel->x - 8, sel->y - 8, sel->w + 16, sel->h + 16}, x, y)) {
          m_g = Gesture{};
          m_g.type = G::Move;
          m_g.serial = ++m_serial;
          m_g.start = {x, y};
          for (const ElPtr& el : elements())
            if (m_selection.contains(el->id)) m_g.original.push_back(el);
          buildSnap(m_g.original);
        } else {
          if (!m_selection.isEmpty()) ++selectionRev;
          m_selection.clear();
          m_g = Gesture{};
          m_g.type = G::Lasso;
          m_g.serial = ++m_serial;
          m_g.lasso = {{x, y}};
        }
      }
    }
    touchOverlay();
    return {};
  }
  if (tl == "text") {
    // In a table: type in that cell.
    const Els& els = elements();
    for (auto it = els.rbegin(); it != els.rend(); ++it)
      if ((*it)->kind == Kind::Table)
        if (auto cell = ed::tableCellAt(**it, x, y)) {
          m_cellRequest = {{"id", (*it)->id}, {"row", cell->first}, {"col", cell->second}};
          return "cell";
        }
  }
  if (tl == "text" || tl == "sticky") {
    const Kind want = tl == "text" ? Kind::Text : Kind::Sticky;
    const Els& els = elements();
    ElPtr existing;
    for (auto it = els.rbegin(); it != els.rend(); ++it)
      if ((*it)->kind == want && rectContains(want == Kind::Text ? pad(frameOfEl(**it), (*it)->fontSize.value_or(20) * 0.6) : bounds(**it), x, y)) {
        existing = *it;
        break;
      }
    if (existing) {
      startTextEdit(existing, false);
    } else {
      El el;
      el.id = newId();
      el.z = nextZ();
      el.kind = want;
      el.text = "";
      el.font = textFont;
      el.bold = textFormat.bold;
      el.italic = textFormat.italic;
      el.align = textFormat.align;
      if (want == Kind::Text) {
        el.x = x;
        el.y = y - 12;
        el.w = std::max(120.0, std::min(420.0, pageWidth() - x - 20));
        el.fontSize = widths.value("text");
        el.color = colors.value("text");
      } else {
        el.x = x - 20;
        el.y = y - 20;
        el.w = 220;
        el.h = 200;
        el.color = colors.value("sticky");
        el.fontSize = widths.value("sticky");
      }
      startTextEdit(makeEl(std::move(el)), true);
    }
    return "edit";
  }
  touchOverlay();
  return {};
}

void EditorCore::move(double x, double y, double p, double t, bool shift, bool alt) {
  m_shift = shift;
  Gesture& g = m_g;
  m_cursor = P2{x, y};
  switch (g.type) {
    case G::None:
      if (tool == "lasso") touchOverlay();
      return;
    case G::Draw: {
      if (g.preview) {
        if (g.held) updateHeld({x, y});
        touchOverlay();
        return;
      }
      if (g.ruler) {
        const P2 q = projectOnto(g.ruler->edge, {x, y}, g.ruler->offset);
        const Pt& last = g.points.back();
        if (std::hypot(q[0] - last[0], q[1] - last[1]) >= 0.4 / zoom) {
          g.points.push_back({q[0], q[1], p});
          g.times.push_back(t);
        }
        setReadout(guidedReadout());
        touchOverlay();
        return;
      }
      g.points.push_back({x, y, p});
      g.times.push_back(t);
      armHold();
      return;
    }
    case G::Erase:
      eraseAlong({x, y});
      return;
    case G::Lasso:
      g.lasso.push_back({x, y});
      break;
    case G::Move:
      g.dx = x - g.start[0];
      g.dy = y - g.start[1];
      g.guides.clear();
      if (g.snapping && !alt) {
        SnapResult s = snapMove(g.anchors, g.targets, g.dx, g.dy, 7 / zoom);
        g.dx = s.dx;
        g.dy = s.dy;
        g.guides = std::move(s.guides);
      }
      break;
    case G::Handle:
      g.preview = makeEl(resizeElement(*g.original[0], g.handle, {x, y}, shift));
      break;
    case G::Transform: {
      // Text only: the sides change the width it wraps to; the corners and
      // the top and bottom scale it evenly.
      const bool allText = std::all_of(g.original.begin(), g.original.end(), [](const ElPtr& e) { return e->isTextish(); });
      const GroupTransform tr = groupTransform(g.box, g.handle, g.start, {x, y}, shift || (allText && g.handle != "e" && g.handle != "w"));
      g.m = tr.m;
      g.sx = tr.sx;
      g.sy = tr.sy;
      g.angle = tr.angle;
      g.previews.clear();
      for (const ElPtr& el : g.original)
        if (el->isTextish()) g.previews.push_back(makeEl(transformed(*el, g)));
      break;
    }
    case G::Shape: {
      const auto shape = buildShape(shapeKind, g.start, {x, y}, shift);
      g.preview = shape ? makeShapeEl(*shape) : nullptr;
      break;
    }
  }
  touchOverlay();
}

void EditorCore::hover(double x, double y) {
  m_cursor = P2{x, y};
  if (tool == "lasso" && !m_selection.isEmpty()) touchOverlay();
}

QString EditorCore::cursorAt(double x, double y) {
  if (tool != "lasso" || m_g.type != G::None) return {};
  const ElPtr single = singleSegment();
  if (single && !handleAt(*single, x, y, 10 / zoom).isEmpty()) return "crosshair";
  const auto content = selectionBounds();
  const auto tbox = single || (content && rectContains(*content, x, y)) ? std::nullopt : transformBox();
  const QString gh = tbox ? groupHandleAt(*tbox, x, y, zoom) : QString();
  if (gh == "rot") return "rotate";
  if (gh == "n" || gh == "s") return "ns";
  if (gh == "e" || gh == "w") return "ew";
  if (gh == "nw" || gh == "se") return "nwse";
  if (gh == "ne" || gh == "sw") return "nesw";
  const auto sel = selectionBounds();
  if (sel && rectContains({sel->x - 8, sel->y - 8, sel->w + 16, sel->h + 16}, x, y)) return "move";
  return {};
}

void EditorCore::up() {
  if (m_g.type == G::None) return;
  // A stroke's shape is worked out while the gesture is still current.
  ElPtr shape;
  std::optional<El> highlight;
  if (m_g.type == G::Draw) {
    stopTimers();
    shape = m_g.preview;
    if (m_g.ruler) {
      setReadout(std::nullopt);
      // Along a straight edge the shape tool makes a clean line between
      // the extremes; along the protractor's arc, the arc itself.
      if (m_g.tool == "shape") {
        Shape geom;
        if (!m_g.ruler->edge.arc) {
          const auto [a, b] = rulerExtent();
          geom.kind = "line";
          geom.points = {a, b};
        } else {
          geom.kind = "path";
          geom.points = flat(m_g.points);
        }
        shape = shapeElement(geom, "shape");
      }
    } else if (!shape && m_g.tool == "shape") {
      shape = refine();
    }
    if (shape && m_g.tool == "highlighter" && shape->shape.kind == "line") highlight = straightHighlight(shape->shape.points);
  }
  Gesture g = std::move(m_g);
  m_g = Gesture{};
  switch (g.type) {
    case G::None:
      break;
    case G::Draw: {
      if (highlight) {
        El hl = *highlight;
        hl.id = newId();
        hl.z = nextZ();
        addElement(makeEl(std::move(hl)));
      } else if (shape && g.tool == "pen" && keepsPen(penStyle)) {
        // A held shape drawn with the pen (dashed, marker, watercolour, neon): pen strokes, not a plain shape.
        if (!g.absorb.empty()) {
          QSet<QString> gone(g.absorb.begin(), g.absorb.end());
          for (const QString& id : gone) m_hidden.remove(id);
          m_recentInk.erase(std::remove_if(m_recentInk.begin(), m_recentInk.end(), [&](const Recent& r) { return gone.contains(r.id); }), m_recentInk.end());
          snapshot();
          Els& els = elements();
          els.erase(std::remove_if(els.begin(), els.end(), [&](const ElPtr& e) { return gone.contains(e->id); }), els.end());
          for (El& el : penStrokes(*shape)) {
            el.z = nextZ();
            els.push_back(makeEl(std::move(el)));
          }
          changed();
        } else {
          std::vector<El> strokes = penStrokes(*shape);
          if (strokes.size() == 1) {
            strokes[0].z = nextZ();
            addElement(makeEl(std::move(strokes[0])));
          } else if (!strokes.empty()) {
            snapshot();
            Els& els = elements();
            for (El& el : strokes) {
              el.z = nextZ();
              els.push_back(makeEl(std::move(el)));
            }
            changed();
          }
        }
      } else if (shape) {
        El s = *shape;
        s.z = nextZ();
        ElPtr placed = makeEl(std::move(s));
        if (!g.absorb.empty()) {
          QSet<QString> gone(g.absorb.begin(), g.absorb.end());
          for (const QString& id : gone) m_hidden.remove(id);
          m_recentInk.erase(std::remove_if(m_recentInk.begin(), m_recentInk.end(), [&](const Recent& r) { return gone.contains(r.id); }), m_recentInk.end());
          snapshot();
          Els& els = elements();
          els.erase(std::remove_if(els.begin(), els.end(), [&](const ElPtr& e) { return gone.contains(e->id); }), els.end());
          els.push_back(placed);
          changed();
        } else {
          addElement(placed);
        }
      } else if (!g.points.empty() && g.tool == "pen" && isTap(g.points, 6 / zoom) && toggleTapeAt(g.points[0][0], g.points[0][1])) {
        // A tap on tape peeks under it (no dot).
      } else if (!g.points.empty() && g.tool == "pen" && !g.ruler && scratchable(penStyle) && scratchAway(g.points)) {
        // Scratched out: the ink under it went, and the scribble isn't kept.
      } else if (!g.points.empty()) {
        const QString tl = g.tool == "highlighter" ? "highlighter" : "pen";
        El el;
        el.id = newId();
        el.z = nextZ();
        el.kind = Kind::Stroke;
        el.tool = tl;
        if (tl == "pen") el.style = penStyle;
        el.color = colors.value(g.tool);
        el.width = widths.value(g.tool);
        el.points = thinPoints(tl == "highlighter" ? draw::steadyHighlight(g.points, widths.value("highlighter")) : g.points, std::min(0.3, 0.6 / zoom));
        if (tl == "pen") el.smooth = streamline;
        if (tl == "pen" && penStyle == "construction") {
          el.extra.insert("dash", penDash == "dot" ? "dot" : "dash");
          if (penGap != 1) el.extra.insert("gap", penGap);
        }
        if (tl == "pen" && penStyle == "tape") {
          // A straight strip, as wide as the tape (points along it, so the eraser can cut it).
          el.width = ink::tapeWidth(float(el.width));
          el.points = straightTape(g.points, el.width);
        }
        // Ink stays on the paper: the parts off its edge are cut away (a stroke
        // that leaves and comes back becomes several).
        const std::vector<std::vector<Pt>> parts = clipToRect(el.points, pageWidth(), pageHeight());
        if (parts.empty()) break;
        if (parts.size() == 1) {
          el.points = parts[0];
          const QString id = el.id;
          addElement(makeEl(std::move(el)));
          if (tl == "pen" && g.tool == "pen") {
            m_recentInk.push_back({id, g.times.back(), flat(g.points)});
            if (m_recentInk.size() > 6) m_recentInk.erase(m_recentInk.begin());
          }
        } else {
          snapshot();
          Els& els = elements();
          for (const std::vector<Pt>& part : parts) {
            El piece = el;
            piece.id = newId();
            piece.z = nextZ();
            piece.points = part;
            els.push_back(makeEl(std::move(piece)));
          }
          changed();
        }
      }
      break;
    }
    case G::Erase:
      if (g.changed) changed();
      break;
    case G::Lasso: {
      double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
      for (const P2& p : g.lasso) x0 = std::min(x0, p[0]), x1 = std::max(x1, p[0]), y0 = std::min(y0, p[1]), y1 = std::max(y1, p[1]);
      const bool tap = x1 - x0 < 6 / zoom && y1 - y0 < 6 / zoom;
      const Els& els = elements();
      if (tap) {
        const double tx = g.lasso[0][0], ty = g.lasso[0][1];
        for (auto it = els.rbegin(); it != els.rend(); ++it)
          // Typed text: near its letters (its box can be much wider, e.g.
          // an Arabic line written from the right, and taps in the empty
          // part shouldn't take it).
          if ((*it)->kind == Kind::Text ? rectContains(pad(frameOfEl(**it), 6 / zoom), tx, ty)
                                        : near(boxOfEl(**it), tx, ty, 8 / zoom) && hitForSelect(**it, tx, ty, 6 / zoom)) {
            m_selection.insert((*it)->id);
            // A table comes with what's written in it (to move it all together).
            if ((*it)->kind == Kind::Table) {
              const Rect t = bounds(**it);
              for (const ElPtr& el : els) {
                const Rect b = bounds(*el);
                if (el != *it && el->z > (*it)->z && b.x >= t.x - 2 && b.y >= t.y - 2 && b.x + b.w <= t.x + t.w + 2 && b.y + b.h <= t.y + t.h + 2) m_selection.insert(el->id);
              }
            }
            break;
          }
      } else {
        // Only what overlaps the lasso can be inside it.
        const Rect box = rectOf(g.lasso);
        const Lasso lasso(g.lasso);
        for (const ElPtr& el : els)
          if (overlaps(boxOfEl(*el), box) && lasso.holds(*el)) m_selection.insert(el->id);
      }
      ++selectionRev;
      break;
    }
    case G::Transform: {
      bool anyText = false;
      for (const ElPtr& el : g.original) {
        m_hidden.remove(el->id);
        anyText = anyText || el->isTextish();
      }
      if (anyText) ++rev;
      if (!g.m.identity()) {
        std::unordered_map<QString, ElPtr> next;
        for (const ElPtr& el : g.original) next.emplace(el->id, makeEl(transformed(*el, g)));
        snapshot();
        for (ElPtr& el : elements())
          if (auto f = next.find(el->id); f != next.end()) el = f->second;
        changed();
      }
      break;
    }
    case G::Handle: {
      const ElPtr& orig = g.original[0];
      m_hidden.remove(orig->id);
      if (g.preview != orig) {
        snapshot();
        for (ElPtr& el : elements())
          if (el->id == orig->id) el = g.preview;
        changed();
      } else {
        ++rev;
      }
      break;
    }
    case G::Move:
      if (std::abs(g.dx) > 0.5 || std::abs(g.dy) > 0.5) {
        snapshot();
        for (ElPtr& el : elements())
          if (m_selection.contains(el->id)) el = makeEl(translate(*el, g.dx, g.dy));
        changed();
      }
      break;
    case G::Shape:
      if (g.preview) {
        El s = *g.preview;
        s.z = nextZ();
        addElement(makeEl(std::move(s)));
      }
      break;
  }
  touchOverlay();
  ++selectionRev;
}

void EditorCore::cancel() {
  const Gesture& g = m_g;
  if (g.type == G::None) return;
  if (g.type == G::Draw) {
    stopTimers();
    for (const QString& id : g.absorb) m_hidden.remove(id);
  }
  if (g.type == G::Handle) m_hidden.remove(g.original[0]->id);
  if (g.type == G::Transform)
    for (const ElPtr& el : g.original) m_hidden.remove(el->id);
  const bool erased = g.type == G::Erase && g.changed;
  m_g = Gesture{};
  if (erased) changed();
  ++rev;
  touchOverlay();
}

bool EditorCore::liveHidden() const { return m_g.type == G::Draw && (m_g.preview || m_g.ruler); }

std::optional<EditorCore::LiveTransform> EditorCore::liveTransform() const {
  if (m_g.type == G::Move) {
    LiveTransform lt;
    for (const ElPtr& e : m_g.original) lt.ids.push_back(e->id);
    lt.m = Affine{1, 0, 0, 1, m_g.dx, m_g.dy};
    return lt;
  }
  if (m_g.type == G::Transform) {
    LiveTransform lt;
    for (const ElPtr& e : m_g.original)
      if (!e->isTextish()) lt.ids.push_back(e->id);
    lt.m = m_g.m;
    return lt;
  }
  return std::nullopt;
}

// ------------------------------------------------------------ holding a stroke still

void EditorCore::stopTimers() {
  m_holdTimer.stop();
  m_stillTimer.stop();
}

void EditorCore::armHold() {
  if (m_g.type != G::Draw) return;
  m_holdTimer.start();
}

std::vector<El> EditorCore::penStrokes(const El& shape) const {
  std::vector<El> out;
  for (const std::vector<P2>& line : shapeLines(shape.shape, widths.value("pen"))) {
    El el;
    el.id = newId();
    el.kind = Kind::Stroke;
    el.tool = "pen";
    el.style = penStyle;
    el.color = colors.value("pen");
    el.width = widths.value("pen");
    el.smooth = 0;
    for (const P2& p : line) el.points.push_back({p[0], p[1], 0.5});
    if (penStyle == "construction") {
      el.extra.insert("dash", penDash == "dot" ? "dot" : "dash");
      if (penGap != 1) el.extra.insert("gap", penGap);
    }
    out.push_back(std::move(el));
  }
  return out;
}

void EditorCore::holdFired() {
  Gesture& g = m_g;
  if (g.type != G::Draw || g.preview || g.points.size() < 4) return;
  // Tape is straight already: holding it still changes nothing.
  if (g.tool == "pen" && penStyle == "tape") return;
  const double now = draw::clockMs();
  std::vector<double> times = g.times;
  times.back() = std::max(now, g.times.back());
  if (!isHeld(times, g.points, HOLD_MS - 50, 4 / zoom)) return;
  auto multi = multiStrokeSymbol();
  ElPtr shape = multi ? multi->shape : refine();
  if (!shape) return;
  g.preview = shape;
  g.absorb = multi ? multi->absorb : std::vector<QString>{};
  for (const QString& id : g.absorb) m_hidden.insert(id);
  if (!g.absorb.empty()) ++rev;
  const P2 anchor{g.points.back()[0], g.points.back()[1]};
  g.held = Held{"transform", shape, anchor, pivotFor(*shape), anchor};
  armModeToggle();
  touchOverlay();
}

ElPtr EditorCore::shapeElement(const Shape& shape, const QString& colorOf) const {
  El el;
  el.id = newId();
  el.z = 0;
  el.kind = Kind::Shape;
  el.shape = shape;
  el.color = colors.value(colorOf);
  el.width = widths.value(colorOf);
  el.fill = std::nullopt;
  return makeEl(std::move(el));
}

ElPtr EditorCore::refine() {
  const Gesture& g = m_g;
  const std::vector<P2> pts = flat(g.points);
  std::optional<Shape> shape;
  if (g.tool == "highlighter") {
    const auto s = recognizeShape(pts);
    const double len = s && s->kind == "line" && s->points.size() >= 2 ? std::hypot(s->points[1][0] - s->points[0][0], s->points[1][1] - s->points[0][1]) : 0;
    if (s && s->kind == "line" && len > std::max(24 / zoom, widths.value("highlighter") * 1.5)) shape = s;
  } else {
    const auto sym = pts.size() > 3 ? matchSymbol({pts}) : std::nullopt;
    shape = sym ? std::optional<Shape>(symbolShape(*sym, rectOf(pts))) : beautify(pts);
  }
  if (!shape) return nullptr;
  ElPtr el = shapeElement(*shape, g.tool);
  el->z = nextZ();
  return el;
}

std::optional<EditorCore::Multi> EditorCore::multiStrokeSymbol() {
  const Gesture& g = m_g;
  if (g.tool != "pen") return std::nullopt;
  const std::vector<P2> pts = flat(g.points);
  const Rect cur = rectOf(pts);
  const double size = std::max({cur.w, cur.h, 12 / zoom});
  struct Item {
    QString id;
    std::vector<P2> pts;
  };
  std::vector<Item> group;
  double t = g.times[0];
  Rect box = cur;
  const Els& els = elements();
  for (int i = int(m_recentInk.size()) - 1; i >= 0 && group.size() < 3; --i) {
    const Recent& r = m_recentInk[size_t(i)];
    if (t - r.t > 2500 || std::none_of(els.begin(), els.end(), [&](const ElPtr& e) { return e->id == r.id; })) break;
    const Rect b = rectOf(r.pts);
    const double reach = size * 0.9 + 8 / zoom;
    const bool isNear = b.x < box.x + box.w + reach && b.x + b.w > box.x - reach && b.y < box.y + box.h + reach && b.y + b.h > box.y - reach;
    if (!isNear) break;
    group.insert(group.begin(), {r.id, r.pts});
    box = unionRects({box, b}).value_or(box);
    t = r.t;
  }
  for (size_t n = group.size(); n >= 1; --n) {
    std::vector<std::vector<P2>> strokes;
    std::vector<QString> used;
    for (size_t k = group.size() - n; k < group.size(); ++k) strokes.push_back(group[k].pts), used.push_back(group[k].id);
    strokes.push_back(pts);
    const auto m = matchSymbol(strokes);
    if (!m) continue;
    std::vector<P2> all;
    for (const auto& s : strokes) all.insert(all.end(), s.begin(), s.end());
    ElPtr shape = shapeElement(symbolShape(*m, rectOf(all)), "pen");
    shape->z = nextZ();
    return Multi{shape, used};
  }
  return std::nullopt;
}

void EditorCore::setReadout(const std::optional<QString>& text) {
  if (text == m_readout) return;
  m_readout = text;
  ++instrumentRev;
}

QString EditorCore::guidedReadout() const {
  const Gesture& g = m_g;
  const EdgeGuide& edge = g.ruler->edge;
  if (!edge.arc) {
    const auto [a, b] = rulerExtent();
    return QString::number(std::hypot(b[0] - a[0], b[1] - a[1]) / (10 * MM), 'f', 1) + " cm";
  }
  double prev = std::atan2(g.points[0][1] - edge.c[1], g.points[0][0] - edge.c[0]);
  double acc = 0, lo = 0, hi = 0;
  for (size_t k = 1; k < g.points.size(); ++k) {
    const Pt& p = g.points[k];
    const double a = std::atan2(p[1] - edge.c[1], p[0] - edge.c[0]);
    double d = a - prev;
    if (d > M_PI) d -= 2 * M_PI;
    if (d < -M_PI) d += 2 * M_PI;
    acc += d;
    lo = std::min(lo, acc);
    hi = std::max(hi, acc);
    prev = a;
  }
  return QString::number(std::floor((hi - lo) * 180 / M_PI + 0.5)) + QStringLiteral("°");
}

std::pair<P2, P2> EditorCore::rulerExtent() const {
  const Gesture& g = m_g;
  const P2 dir = !g.ruler->edge.arc ? g.ruler->edge.dir : P2{1, 0};
  const Pt* lo = &g.points[0];
  const Pt* hi = &g.points[0];
  auto t = [&](const Pt& p) { return p[0] * dir[0] + p[1] * dir[1]; };
  for (const Pt& p : g.points) {
    if (t(p) < t(*lo)) lo = &p;
    if (t(p) > t(*hi)) hi = &p;
  }
  return {P2{(*lo)[0], (*lo)[1]}, P2{(*hi)[0], (*hi)[1]}};
}

// ------------------------------------------------------------ instruments

QVariantMap EditorCore::instrument() const {
  QJsonObject o = m_ruler.describe(zoom, viewWidth);
  o["rev"] = double(instrumentRev);
  if (m_ruler.visible) {
    o["readout"] = m_readout ? QJsonValue(*m_readout) : QJsonValue(QJsonValue::Null);
    o["page"] = active;
  }
  return o.toVariantMap();
}

void EditorCore::showInstrument(const QString& kind, double cx, double cy) {
  m_ruler.setKind(kind);
  if (!m_ruler.visible) m_ruler.show(cx, cy);
  ++instrumentRev;
}

void EditorCore::hideInstrument() {
  m_ruler.hide();
  ++instrumentRev;
}

void EditorCore::placeInstrument(double cx, double cy, double angle) {
  m_ruler.cx = cx;
  m_ruler.cy = cy;
  m_ruler.angle = angle;
  ++instrumentRev;
}

void EditorCore::setArm(int i, double deg) {
  if (i < 0 || i > 1) return;
  m_ruler.arms[i] = std::min(180.0, std::max(0.0, deg));
  ++instrumentRev;
}

El EditorCore::straightHighlight(const std::vector<P2>& line) const {
  const P2 a = line.front();
  const P2 b = line.back();
  const double p = m_g.points[0][2];
  El el;
  el.id = "live";
  el.z = 0;
  el.kind = Kind::Stroke;
  el.tool = "highlighter";
  el.color = colors.value("highlighter");
  el.width = widths.value("highlighter");
  el.points = {{a[0], a[1], p}, {b[0], b[1], p}};
  return el;
}

P2 EditorCore::pivotFor(const El& el) const {
  const Shape& s = el.shape;
  if ((s.kind == "line" || s.kind == "arrow") && !s.points.empty()) return s.points[0];
  const Rect r = geomBox(el);
  return {r.x + r.w / 2, r.y + r.h / 2};
}

void EditorCore::armModeToggle() {
  if (!m_g.held) return;
  m_stillTimer.stop();
  if (m_g.tool == "highlighter") return;
  m_stillTimer.start();
}

void EditorCore::stillFired() {
  Gesture& g = m_g;
  if (g.type != G::Draw || !g.held || !g.preview) return;
  Held& held = *g.held;
  held.mode = held.mode == "transform" ? "move" : "transform";
  held.base = g.preview;
  held.anchor = held.still;
  held.pivot = pivotFor(*g.preview);
  touchOverlay();
}

void EditorCore::updateHeld(P2 p) {
  Held& h = *m_g.held;
  if (std::hypot(p[0] - h.still[0], p[1] - h.still[1]) > 4 / zoom) {
    h.still = p;
    armModeToggle();
  }
  Shape shape;
  if (h.mode == "move") {
    shape = transformShape(h.base->shape, h.pivot, 0, 1, p[0] - h.anchor[0], p[1] - h.anchor[1]);
  } else {
    const P2 v0{h.anchor[0] - h.pivot[0], h.anchor[1] - h.pivot[1]};
    const P2 v1{p[0] - h.pivot[0], p[1] - h.pivot[1]};
    const double l0 = std::hypot(v0[0], v0[1]);
    if (l0 < 3 / zoom) {
      shape = h.base->shape;
    } else {
      const double scale = std::max(0.05, std::min(20.0, std::hypot(v1[0], v1[1]) / l0));
      const double angle = std::atan2(v1[1], v1[0]) - std::atan2(v0[1], v0[0]);
      shape = transformShape(h.base->shape, h.pivot, angle, scale);
    }
  }
  El next = *h.base;
  next.shape = shape;
  m_g.preview = makeEl(std::move(next));
}

void EditorCore::buildSnap(const Els& moving) {
  auto isPlainText = [](const El& el) { return el.kind == Kind::Text && !trimmed(el.text).isEmpty() && !el.rot(); };
  Els texts;
  for (const ElPtr& el : moving)
    if (isPlainText(*el)) texts.push_back(el);
  QSet<QString> ids;
  for (const ElPtr& el : moving) ids.insert(el->id);
  if (texts.empty()) {
    // Anything else: its box, against the page's centre and middle and the
    // boxes of the other things on the page (the nearest few hundred).
    const auto box = selectionBounds();
    if (!box || moving.empty()) return;
    std::vector<Target> targets = paperTargets({}, 0, 0, false, 0, pageWidth(), pageHeight());
    int n = 0;
    for (const ElPtr& el : elements()) {
      if (ids.contains(el->id) || ++n > 400) continue;
      for (const Anchor& a : boxAnchors(bounds(*el))) targets.push_back(a);
    }
    m_g.snapping = true;
    m_g.anchors = boxAnchors(*box);
    m_g.targets = std::move(targets);
    return;
  }
  std::vector<Target> targets;
  for (const ElPtr& el : elements())
    if (!ids.contains(el->id) && isPlainText(*el))
      for (const Anchor& a : textAnchors(*el)) targets.push_back(a);
  const draw::PaperLayout paper = draw::paperLayout(page()["template"].toString("blank"), pageWidth(), pageHeight());
  for (const Target& t : paperTargets(paper.rules, paper.ruleFrom, paper.ruleTo, paper.hasMargin, paper.margin, pageWidth(), pageHeight())) targets.push_back(t);
  std::vector<Anchor> anchors;
  for (const ElPtr& el : texts)
    for (const Anchor& a : textAnchors(*el)) anchors.push_back(a);
  m_g.snapping = true;
  m_g.anchors = std::move(anchors);
  m_g.targets = std::move(targets);
}

ElPtr EditorCore::makeShapeEl(const Shape& shape) {
  const QString color = colors.value("shape");
  ElPtr el = shapeElement(shape, "shape");
  if (shapeFill && isClosedShape(shape)) el->fill = color + "33";
  return el;
}

// ------------------------------------------------------------ erasing

void EditorCore::eraseAlong(P2 p) {
  if (m_g.type != G::Erase) return;
  const P2 from = m_g.last.value_or(p);
  const double step = std::max(1.0, widths.value("eraser") / 4);
  const int n = std::max(1, int(std::ceil(std::hypot(p[0] - from[0], p[1] - from[1]) / step)));
  for (int i = m_g.last ? 1 : 0; i <= n; ++i) eraseAt(from[0] + ((p[0] - from[0]) * i) / n, from[1] + ((p[1] - from[1]) * i) / n);
  m_g.last = p;
}

void EditorCore::eraseAt(double x, double y) {
  if (m_g.type != G::Erase) return;
  const double radius = widths.value("eraser") / 2;
  bool any = false;
  Els next;
  Els& els = elements();
  next.reserve(els.size());
  for (const ElPtr& el : els) {
    const double w = el->kind == Kind::Stroke || el->kind == Kind::Shape ? el->width : 0;
    if (el->kind == Kind::Image || el->kind == Kind::Graph || el->kind == Kind::Table || !near(boxOfEl(*el), x, y, radius + 2 + w)) {
      next.push_back(el);
      continue;
    }
    if (eraserMode == "stroke" || el->isTextish()) {
      if (hitTest(*el, x, y, radius)) any = true;
      else next.push_back(el);
      continue;
    }
    auto pieces = eraseElement(*el, x, y, radius);
    if (!pieces) {
      next.push_back(el);
    } else {
      any = true;
      for (El& piece : *pieces) next.push_back(makeEl(std::move(piece)));
    }
  }
  if (!any) return;
  if (!m_g.changed) snapshot();
  m_g.changed = true;
  els = std::move(next);
  pages[size_t(active)].dirty = true;
  ++rev;
}

// ------------------------------------------------------------ selection

const EditorCore::SelCache& EditorCore::selected() {
  const Els& els = elements();
  const SelCache* c = m_selCache ? &*m_selCache : nullptr;
  if (c && c->els == &els && c->len == els.size() && c->size == int(m_selection.size()) && c->rev == rev && c->selRev == selectionRev && c->active == active) return *c;
  SelCache next;
  next.els = &els;
  next.len = els.size();
  next.rev = rev;
  next.selRev = selectionRev;
  next.active = active;
  next.size = int(m_selection.size());
  if (!m_selection.isEmpty())
    for (const ElPtr& el : els)
      if (m_selection.contains(el->id)) next.list.push_back(el);
  if (!next.list.empty()) {
    std::vector<Rect> rs;
    for (const ElPtr& el : next.list) rs.push_back(frameOfEl(*el));
    next.box = unionRects(rs);
  }
  m_selCache = std::move(next);
  return *m_selCache;
}

Els EditorCore::selectedList() { return selected().list; }

std::optional<Rect> EditorCore::selectionBounds() {
  if (m_selection.isEmpty()) return std::nullopt;
  return selected().box;
}

void EditorCore::clearSelection() {
  if (m_selection.isEmpty()) return;
  m_selection.clear();
  touchOverlay();
  ++selectionRev;
}

void EditorCore::selectAll() {
  for (const ElPtr& el : elements()) m_selection.insert(el->id);
  touchOverlay();
  ++selectionRev;
}

ElPtr EditorCore::singleShape() {
  if (m_selection.size() != 1) return nullptr;
  const Els& list = selected().list;
  if (list.empty()) return nullptr;
  return list[0]->kind == Kind::Shape ? list[0] : nullptr;
}

ElPtr EditorCore::singleSegment() {
  ElPtr el = singleShape();
  return el && (el->shape.kind == "line" || el->shape.kind == "arrow") && isResizable(*el) ? el : nullptr;
}

std::optional<Rect> EditorCore::transformBox() {
  if (m_selection.isEmpty() || singleSegment()) return std::nullopt;
  const auto sel = selectionBounds();
  if (!sel) return std::nullopt;
  return Rect{sel->x - 6, sel->y - 6, sel->w + 12, sel->h + 12};
}

QVariantMap EditorCore::selectionInfo() {
  const Els els = selected().list;
  if (els.empty() || m_g.type != G::None) return {{"count", int(els.size())}};
  Els texts;
  for (const ElPtr& el : els)
    if (el->isTextish()) texts.push_back(el);
  const ElPtr single = singleShape();
  const Rect r = *selectionBounds();
  QSet<QString> cols, fonts;
  for (const ElPtr& el : els)
    if (el->kind == Kind::Stroke || el->kind == Kind::Shape || el->kind == Kind::Text) cols.insert(el->color.toLower());
  for (const ElPtr& t : texts) fonts.insert(t->font.value_or("sans"));
  auto all = [&](auto pred) { return std::all_of(texts.begin(), texts.end(), pred); };
  QVariantMap out;
  out["count"] = int(els.size());
  out["page"] = active;
  out["box"] = QVariantMap{{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}};
  out["color"] = cols.size() == 1 ? QVariant(*cols.begin()) : QVariant();
  out["hasInk"] = std::any_of(els.begin(), els.end(), [](const ElPtr& e) { return e->kind == Kind::Stroke && e->tool == "pen"; });
  out["texts"] = int(texts.size());
  out["font"] = fonts.size() == 1 ? QVariant(*fonts.begin()) : QVariant();
  out["bold"] = !texts.empty() && all([](const ElPtr& t) { return t->bold.value_or(false); });
  out["italic"] = !texts.empty() && all([](const ElPtr& t) { return t->italic.value_or(false); });
  out["align"] = all([](const ElPtr& t) { return t->align.value_or("left") == "center"; }) ? "center" : all([](const ElPtr& t) { return t->align && *t->align == "right"; }) ? "right" : "left";
  QVariant list;
  if (!texts.empty() && all([](const ElPtr& t) { return currentList(t->text) == "bullet"; })) list = "bullet";
  else if (!texts.empty() && all([](const ElPtr& t) { return currentList(t->text) == "number"; })) list = "number";
  out["list"] = list;
  out["shape"] = single ? QVariant(QVariantMap{{"kind", single->shape.kind}, {"closed", isClosedShape(single->shape)}, {"width", single->width}, {"fill", bool(single->fill)}}) : QVariant();
  if (els.size() == 1 && els[0]->kind == Kind::Graph) {
    const El& g = *els[0];
    out["graph"] = QVariantMap{{"functions", g.text}, {"xmin", g.extra.value("xmin").toDouble(-5)}, {"xmax", g.extra.value("xmax").toDouble(5)},
                               {"ymin", g.extra.value("ymin").toDouble(-5)}, {"ymax", g.extra.value("ymax").toDouble(5)}, {"grid", g.extra.value("grid").toBool(true)}};
  }
  // One picture: its box, which part of it shows and how it's cut.
  if (els.size() == 1 && els[0]->kind == Kind::Image) {
    const El& im = *els[0];
    QVariantList crop{0.0, 0.0, 1.0, 1.0};
    const QJsonArray c = im.extra.value("crop").toArray();
    if (c.size() == 4) crop = c.toVariantList();
    const QImage* pic = draw::imageFor(im.src);
    out["image"] = QVariantMap{{"id", im.id}, {"x", im.x}, {"y", im.y}, {"w", im.w}, {"h", im.h}, {"rotation", im.rot()},
                               {"crop", crop}, {"mask", im.extra.value("mask").toString("rect")},
                               {"naturalW", pic ? pic->width() : 0}, {"naturalH", pic ? pic->height() : 0}};
  }
  // A link (the same one on everything selected), or "mixed" when some differ.
  {
    QSet<QString> links;
    for (const ElPtr& e : els) links.insert(QString::fromUtf8(QJsonDocument(e->extra.value("link").toObject()).toJson(QJsonDocument::Compact)));
    if (links.size() == 1) {
      const QJsonObject l = els[0]->extra.value("link").toObject();
      if (!l.isEmpty()) out["link"] = l.toVariantMap();
    } else {
      out["link"] = QVariantMap{{"kind", "mixed"}};
    }
  }
  out["hasText"] = std::any_of(els.begin(), els.end(), [](const ElPtr& e) { return e->kind == Kind::Text && !e->text.trimmed().isEmpty(); });
  // A table (with what's written in it), or a grid drawn by hand that can become one.
  const ElPtr* table = nullptr;
  int tables = 0;
  for (const ElPtr& e : els)
    if (e->kind == Kind::Table) table = &e, ++tables;
  if (tables == 1) {
    const TableData t = tableData(**table);
    out["table"] = QVariantMap{{"id", (*table)->id}, {"rows", t.rowCount()}, {"cols", t.colCount()}, {"header", t.header}};
  } else if (tables == 0 && els.size() >= 2 && els.size() <= 400) {
    std::vector<const El*> list;
    for (const ElPtr& e : els) list.push_back(e.get());
    if (findDrawnGrid(list)) out["drawnTable"] = true;
  }
  return out;
}

void EditorCore::styleSelectedText(const QVariantMap& change) {
  Els& els = elements();
  if (std::none_of(els.begin(), els.end(), [&](const ElPtr& e) { return m_selection.contains(e->id) && e->isTextish(); })) return;
  snapshot();
  for (ElPtr& el : elements()) {
    if (!m_selection.contains(el->id) || !el->isTextish()) continue;
    El next = *el;
    if (!change.value("font").toString().isEmpty()) next.font = change.value("font").toString();
    if (change.contains("bold") && !change.value("bold").isNull()) next.bold = change.value("bold").toBool();
    if (change.contains("italic") && !change.value("italic").isNull()) next.italic = change.value("italic").toBool();
    if (!change.value("align").toString().isEmpty()) next.align = change.value("align").toString();
    if (!change.value("list").toString().isEmpty()) next.text = toggleList(el->text, change.value("list").toString());
    if (const double sc = change.value("scale").toDouble(); sc != 0) {
      const double size = (el->kind == Kind::Text ? el->fontSize.value_or(0) : el->fontSize.value_or(18)) * sc;
      next.fontSize = std::floor(std::min(96.0, std::max(8.0, size)) + 0.5);
    }
    el = makeEl(std::move(next));
  }
  changed();
}

void EditorCore::updateSelectedShape(const QString& change, double value) {
  const ElPtr single = singleShape();
  if (!single) return;
  snapshot();
  for (ElPtr& el : elements()) {
    if (el->id != single->id || el->kind != Kind::Shape) continue;
    El next = *el;
    if (change == "width") {
      if (!std::isnan(value)) next.width = value;
    } else if (change == "fill") {
      if (el->fill) next.fill = std::nullopt;
      else next.fill = el->color + "33";
    } else {
      next = convertShape(*el, change);
    }
    el = makeEl(std::move(next));
  }
  changed();
}

void EditorCore::deleteSelection() {
  if (m_selection.isEmpty()) return;
  snapshot();
  Els& els = elements();
  els.erase(std::remove_if(els.begin(), els.end(), [&](const ElPtr& e) { return m_selection.contains(e->id); }), els.end());
  m_selection.clear();
  changed();
}

void EditorCore::recolorSelection(const QString& color) {
  snapshot();
  for (ElPtr& el : elements()) {
    if (!m_selection.contains(el->id)) continue;
    if (el->kind == Kind::Shape) {
      El n = *el;
      n.color = color;
      n.fill = el->fill ? std::optional<QString>(color + "33") : std::nullopt;
      el = makeEl(std::move(n));
    } else if (el->kind == Kind::Stroke || el->kind == Kind::Text) {
      El n = *el;
      n.color = color;
      el = makeEl(std::move(n));
    }
  }
  changed();
}

std::vector<El>& EditorCore::clipboard() {
  static std::vector<El> shared;
  return shared;
}

QJsonObject EditorCore::selectionAsPiece() {
  const Els sel = selected().list;
  if (sel.empty()) return {};
  std::vector<Rect> rs;
  for (const ElPtr& el : sel) rs.push_back(boxOfEl(*el));
  const Rect box = *unionRects(rs);
  QJsonArray els;
  for (const ElPtr& el : sel) {
    El c = translate(*el, -box.x, -box.y);
    QJsonObject o = c.toJson();
    o.remove("id");
    els.append(o);
  }
  return {{"elements", els}, {"width", box.w}, {"height", box.h}};
}

void EditorCore::insertPiece(const QJsonArray& els, double x, double y) {
  if (els.isEmpty()) return;
  snapshot();
  double z = nextZ();
  QSet<QString> sel;
  for (const QJsonValue& v : els) {
    El c = translate(El::fromJson(v.toObject()), x, y);
    c.id = newId();
    c.z = z++;
    sel.insert(c.id);
    elements().push_back(makeEl(std::move(c)));
  }
  m_selection = sel;
  ++selectionRev;
  changed();
}

int EditorCore::copySelection() {
  clipboard().clear();
  for (const ElPtr& el : elements())
    if (m_selection.contains(el->id)) clipboard().push_back(*el);
  return int(clipboard().size());
}

int EditorCore::cutSelection() {
  const int n = copySelection();
  deleteSelection();
  return n;
}

int EditorCore::pasteClipboard() {
  if (clipboard().empty()) return 0;
  placeCopies(clipboard());
  return int(clipboard().size());
}

void EditorCore::duplicateSelection() {
  std::vector<El> items;
  for (const ElPtr& el : elements())
    if (m_selection.contains(el->id)) items.push_back(*el);
  if (!items.empty()) placeCopies(items);
}

void EditorCore::placeCopies(const std::vector<El>& items) {
  snapshot();
  double z = nextZ();
  QSet<QString> sel;
  Els& els = elements();
  for (const El& el : items) {
    El c = translate(el, 24, 24);
    c.id = newId();
    c.z = z++;
    sel.insert(c.id);
    els.push_back(makeEl(std::move(c)));
  }
  m_selection = sel;
  changed();
}

void EditorCore::insertImage(const QString& src, double naturalW, double naturalH, double cx, double cy) {
  const double maxW = pageWidth() * 0.6;
  const double scale = std::min(1.0, maxW / naturalW);
  const double w = naturalW * scale, hh = naturalH * scale;
  El el;
  el.id = newId();
  el.z = nextZ();
  el.kind = Kind::Image;
  el.x = std::max(0.0, cx - w / 2);
  el.y = std::max(0.0, cy - hh / 2);
  el.w = w;
  el.h = hh;
  el.src = src;
  const QString id = el.id;
  addElement(makeEl(std::move(el)));
  m_selection = {id};
  ++selectionRev;
}

void EditorCore::insertText(const QString& value, double x, double y, double w) {
  El el;
  el.id = newId();
  el.z = nextZ();
  el.kind = Kind::Text;
  el.x = std::max(0.0, x);
  el.y = std::max(0.0, y);
  el.w = std::max(80.0, std::min(w, pageWidth() - el.x));
  el.text = value;
  el.fontSize = 16;
  el.color = "#1d1f23";
  el.font = textFont;
  const QString id = el.id;
  addElement(makeEl(std::move(el)));
  m_selection = {id};
  ++selectionRev;
}

namespace {
void applyGraphSpec(El& el, const QVariantMap& spec) {
  if (spec.contains("functions")) el.text = spec.value("functions").toString().trimmed();
  for (const char* k : {"xmin", "xmax", "ymin", "ymax"})
    if (spec.contains(k)) el.extra[k] = spec.value(k).toDouble();
  if (spec.contains("grid")) el.extra["grid"] = spec.value("grid").toBool();
}
}  // namespace

void EditorCore::insertGraph(const QVariantMap& spec, double x, double y, double w, double h) {
  El el;
  el.id = newId();
  el.z = nextZ();
  el.kind = Kind::Graph;
  el.color = "#111827";
  el.x = std::max(0.0, std::min(x, pageWidth() - w));
  el.y = std::max(0.0, std::min(y, pageHeight() - h));
  el.w = w;
  el.h = h;
  el.extra = QJsonObject{{"xmin", -5}, {"xmax", 5}, {"ymin", -5}, {"ymax", 5}, {"grid", true}};
  applyGraphSpec(el, spec);
  const QString id = el.id;
  addElement(makeEl(std::move(el)));
  m_selection = {id};
  ++selectionRev;
}

bool EditorCore::updateSelectedGraph(const QVariantMap& spec) {
  const Els sel = selected().list;
  if (sel.size() != 1 || sel[0]->kind != Kind::Graph) return false;
  snapshot();
  for (ElPtr& el : elements())
    if (el->id == sel[0]->id) {
      El next = *el;
      applyGraphSpec(next, spec);
      el = makeEl(std::move(next));
    }
  ++selectionRev;
  changed();
  return true;
}

// ------------------------------------------------------------ words (spelling)

void EditorCore::replaceOnPage(int pi, const std::function<bool(const El&)>& drop, std::vector<El> add) {
  if (pi < 0 || pi >= int(pages.size())) return;
  if (pi == active) finishTextEdit();
  Els& els = load(pi);
  els.erase(std::remove_if(els.begin(), els.end(), [&](const ElPtr& e) { if (drop(*e)) { m_selection.remove(e->id); return true; } return false; }), els.end());
  double z = 0;
  for (const ElPtr& e : els) z = std::max(z, e->z);
  for (El& e : add) {
    e.z = ++z;
    els.push_back(makeEl(std::move(e)));
  }
  pages[size_t(pi)].dirty = true;
  ++rev;
  touchOverlay();
  ++selectionRev;
}

void EditorCore::selectIds(int pi, const QStringList& ids) {
  activate(pi);
  m_selection.clear();
  QSet<QString> want(ids.begin(), ids.end());
  for (const ElPtr& el : load(pi))
    if (want.contains(el->id)) m_selection.insert(el->id);
  ++selectionRev;
}

std::vector<EditorCore::InkWord> EditorCore::inkWords(int pi, bool selectionOnly) {
  struct S {
    Rect b;
    QString id;
  };
  std::vector<S> big, small;
  for (const ElPtr& el : load(pi)) {
    if (el->kind != Kind::Stroke || el->tool != "pen" || el->points.empty()) continue;
    if (selectionOnly && !m_selection.contains(el->id)) continue;
    const QString style = el->style.value_or("fountain");
    if (style == "tape" || style == "construction") continue;
    const Rect b = bounds(*el);
    // Long lines and drawings aren't words.
    if (b.w > 900 || b.h > 260) continue;
    (std::max(b.w, b.h) < 7 ? small : big).push_back({b, el->id});
  }
  // Lines: strokes that share most of their height.
  std::sort(big.begin(), big.end(), [](const S& a, const S& b) { return a.b.y + a.b.h / 2 < b.b.y + b.b.h / 2; });
  struct Line {
    double top, bottom;
    std::vector<S> items;
  };
  std::vector<Line> lines;
  for (const S& s : big) {
    Line* best = nullptr;
    double bestOverlap = 0;
    for (size_t i = lines.size() > 4 ? lines.size() - 4 : 0; i < lines.size(); ++i) {
      Line& l = lines[i];
      const double overlap = std::min(l.bottom, s.b.y + s.b.h) - std::max(l.top, s.b.y);
      const double need = 0.4 * std::min(l.bottom - l.top, s.b.h);
      if (overlap >= need && overlap > bestOverlap) best = &l, bestOverlap = overlap;
    }
    if (best) {
      best->top = std::min(best->top, s.b.y);
      best->bottom = std::max(best->bottom, s.b.y + s.b.h);
      best->items.push_back(s);
    } else {
      lines.push_back({s.b.y, s.b.y + s.b.h, {s}});
    }
  }
  std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.top < b.top; });
  // Words: strokes in a line closer than about a third of its height.
  std::vector<InkWord> out;
  for (Line& l : lines) {
    std::sort(l.items.begin(), l.items.end(), [](const S& a, const S& b) { return a.b.x < b.b.x; });
    std::vector<double> hs;
    for (const S& s : l.items) hs.push_back(s.b.h);
    std::nth_element(hs.begin(), hs.begin() + hs.size() / 2, hs.end());
    const double gap = std::max(5.0, 0.38 * hs[hs.size() / 2]);
    InkWord w{l.items[0].b, {l.items[0].id}};
    for (size_t i = 1; i < l.items.size(); ++i) {
      const S& s = l.items[i];
      if (s.b.x - (w.box.x + w.box.w) < gap) {
        w.box = *unionRects({w.box, s.b});
        w.ids << s.id;
      } else {
        out.push_back(w);
        w = {s.b, {s.id}};
      }
    }
    out.push_back(w);
  }
  // Dots and accents: with the word they're over or next to.
  for (const S& s : small) {
    InkWord* best = nullptr;
    double bestD = 30;
    for (InkWord& w : out) {
      const double cx = s.b.x + s.b.w / 2, cy = s.b.y + s.b.h / 2;
      const double dx = std::max({w.box.x - cx, 0.0, cx - (w.box.x + w.box.w)});
      const double dy = std::max({w.box.y - cy, 0.0, cy - (w.box.y + w.box.h)});
      const double d = std::hypot(dx, dy);
      if (d < bestD) best = &w, bestD = d;
    }
    if (best) {
      best->box = *unionRects({best->box, s.b});
      best->ids << s.id;
    }
  }
  if (out.size() > 400) out.resize(400);
  return out;
}

// ------------------------------------------------------------ links

bool EditorCore::linkSelection(const QJsonObject& link) {
  const Els sel = selected().list;
  if (sel.empty()) return false;
  QSet<QString> ids;
  for (const ElPtr& e : sel) ids.insert(e->id);
  snapshot();
  for (ElPtr& el : elements())
    if (ids.contains(el->id)) {
      El next = *el;
      if (link.isEmpty()) next.extra.remove("link");
      else next.extra["link"] = link;
      el = makeEl(std::move(next));
    }
  ++selectionRev;
  changed();
  return true;
}

std::vector<std::pair<Rect, QJsonObject>> EditorCore::linksOn(int page) {
  // Linked things with the same link, near each other, are one link.
  std::vector<std::pair<Rect, QJsonObject>> out;
  std::vector<QString> keys;
  for (const ElPtr& el : load(page)) {
    const QJsonObject l = el->extra.value("link").toObject();
    if (l.isEmpty()) continue;
    const QString key = QString::fromUtf8(QJsonDocument(l).toJson(QJsonDocument::Compact));
    const Rect b = bounds(*el);
    bool joined = false;
    for (size_t i = 0; i < out.size() && !joined; ++i) {
      if (keys[i] != key) continue;
      Rect& r = out[i].first;
      const double gap = 40;
      if (b.x < r.x + r.w + gap && b.x + b.w > r.x - gap && b.y < r.y + r.h + gap && b.y + b.h > r.y - gap) {
        r = *unionRects({r, b});
        joined = true;
      }
    }
    if (!joined) {
      out.push_back({b, l});
      keys.push_back(key);
    }
  }
  return out;
}

// ------------------------------------------------------------ pictures

QString EditorCore::selectedImageSrc() {
  const Els sel = selected().list;
  return sel.size() == 1 && sel[0]->kind == Kind::Image ? sel[0]->src : QString();
}

bool EditorCore::cropSelectedImage(double x, double y, double w, double h, const QVariantList& crop, const QString& mask) {
  const Els sel = selected().list;
  if (sel.size() != 1 || sel[0]->kind != Kind::Image || crop.size() != 4 || w < 2 || h < 2) return false;
  snapshot();
  for (ElPtr& el : elements())
    if (el->id == sel[0]->id) {
      El next = *el;
      next.x = x;
      next.y = y;
      next.w = w;
      next.h = h;
      const double cx = std::clamp(crop[0].toDouble(), 0.0, 1.0), cy = std::clamp(crop[1].toDouble(), 0.0, 1.0);
      const double cw = std::clamp(crop[2].toDouble(), 0.01, 1.0 - cx), ch = std::clamp(crop[3].toDouble(), 0.01, 1.0 - cy);
      // The whole picture, uncut: nothing to keep.
      if (cx < 1e-4 && cy < 1e-4 && cw > 1 - 1e-4 && ch > 1 - 1e-4) next.extra.remove("crop");
      else next.extra["crop"] = QJsonArray{cx, cy, cw, ch};
      if (mask == "rounded" || mask == "circle") next.extra["mask"] = mask;
      else next.extra.remove("mask");
      next.cachedBounds.reset();
      next.cachedFrame.reset();
      el = makeEl(std::move(next));
    }
  ++selectionRev;
  changed();
  return true;
}

// ------------------------------------------------------------ tables

void EditorCore::insertTable(int rows, int cols, double x, double y, double w) {
  rows = std::clamp(rows, 1, 40);
  cols = std::clamp(cols, 1, 20);
  El el;
  el.id = newId();
  el.z = nextZ();
  el.kind = Kind::Table;
  el.color = "#374151";
  el.width = 1.5;
  // A little smaller than typed text, so the cells hold more.
  el.fontSize = std::clamp(std::round(widths.value("text", 20) * 0.8), 11.0, 24.0);
  if (!textFont.isEmpty() && textFont != "sans") el.font = textFont;
  el.w = std::min(w, pageWidth() - 20);
  el.h = rows * *el.fontSize * 2.1;
  el.x = std::max(0.0, std::min(x, pageWidth() - el.w));
  el.y = std::max(0.0, std::min(y, pageHeight() - el.h));
  TableData t;
  t.cols.assign(cols, 1.0);
  t.rows.assign(rows, 1.0);
  t.cells.assign(rows, QStringList(QVector<QString>(cols).toList()));
  t.header = rows > 1;
  setTableData(el, t);
  const QString id = el.id;
  addElement(makeEl(std::move(el)));
  m_selection = {id};
  ++selectionRev;
}

bool EditorCore::tableFromSelection() {
  const Els sel = selected().list;
  std::vector<const El*> list;
  for (const ElPtr& e : sel) list.push_back(e.get());
  const auto grid = findDrawnGrid(list);
  if (!grid) return false;
  El el;
  el.id = newId();
  el.kind = Kind::Table;
  el.color = grid->color.isEmpty() ? "#374151" : grid->color;
  el.width = grid->width;
  el.fontSize = std::clamp(std::round(widths.value("text", 20) * 0.8), 11.0, 24.0);
  if (!textFont.isEmpty() && textFont != "sans") el.font = textFont;
  el.x = grid->frame.x, el.y = grid->frame.y, el.w = grid->frame.w, el.h = grid->frame.h;
  // Under what's written in it.
  double z = INFINITY;
  for (const ElPtr& e : sel)
    if (grid->lineIds.contains(e->id)) z = std::min(z, e->z);
  el.z = std::isfinite(z) ? z : nextZ();
  TableData t;
  for (size_t i = 0; i + 1 < grid->xs.size(); ++i) t.cols.push_back(grid->xs[i + 1] - grid->xs[i]);
  for (size_t i = 0; i + 1 < grid->ys.size(); ++i) t.rows.push_back(grid->ys[i + 1] - grid->ys[i]);
  t.cells.assign(t.rows.size(), QStringList(QVector<QString>(int(t.cols.size())).toList()));
  setTableData(el, t);
  // Typed text in a cell goes into it.
  QSet<QString> gone = grid->lineIds;
  for (const ElPtr& e : sel) {
    if (e->kind != Kind::Text || e->rot() != 0 || e->text.trimmed().isEmpty()) continue;
    const Rect b = bounds(*e);
    if (auto cell = ed::tableCellAt(el, b.x + b.w / 2, b.y + b.h / 2)) {
      const QString before = t.cell(cell->first, cell->second);
      t.setCell(cell->first, cell->second, before.isEmpty() ? e->text.trimmed() : before + "\n" + e->text.trimmed());
      gone.insert(e->id);
    }
  }
  setTableData(el, t);
  fitTableRows(el);
  snapshot();
  Els& els = elements();
  els.erase(std::remove_if(els.begin(), els.end(), [&](const ElPtr& e) { return gone.contains(e->id); }), els.end());
  const QString id = el.id;
  ElPtr made = makeEl(std::move(el));
  els.insert(std::upper_bound(els.begin(), els.end(), made, [](const ElPtr& a, const ElPtr& b) { return a->z < b->z; }), made);
  QSet<QString> keep;
  for (const ElPtr& e : sel)
    if (!gone.contains(e->id)) keep.insert(e->id);
  keep.insert(id);
  m_selection = keep;
  ++selectionRev;
  changed();
  return true;
}

namespace {
ElPtr findTable(const Els& els, const QString& id) {
  for (const ElPtr& e : els)
    if (e->id == id && e->kind == Kind::Table) return e;
  return nullptr;
}
}  // namespace

QVariantMap EditorCore::tableCell(const QString& id, int row, int col) {
  const ElPtr el = findTable(elements(), id);
  if (!el) return {};
  const TableData t = tableData(*el);
  if (row < 0 || col < 0 || row >= t.rowCount() || col >= t.colCount()) return {};
  const Rect r = tableCellRect(*el, row, col);
  return {{"id", id}, {"row", row}, {"col", col}, {"rows", t.rowCount()}, {"cols", t.colCount()}, {"page", active},
          {"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}, {"text", t.cell(row, col)}, {"header", t.header && row == 0},
          {"fontSize", el->fontSize.value_or(18)}, {"font", el->font.value_or(QString())}, {"color", el->extra.value("textColor").toString("#111827")}};
}

QVariantMap EditorCore::tableCellAt(double x, double y) {
  const Els& els = elements();
  for (auto it = els.rbegin(); it != els.rend(); ++it)
    if ((*it)->kind == Kind::Table)
      if (auto cell = ed::tableCellAt(**it, x, y)) return tableCell((*it)->id, cell->first, cell->second);
  return {};
}

namespace {
/// Replaces the table `id` with a changed copy (`change` returns false: nothing to do).
template <class F>
bool editTable(EditorCore& ed, Els& els, const QString& id, F change) {
  for (ElPtr& e : els)
    if (e->id == id && e->kind == Kind::Table) {
      El next = *e;
      if (!change(next)) return false;
      ed.snapshot();
      for (ElPtr& x : els)
        if (x->id == id) x = makeEl(std::move(next));
      return true;
    }
  return false;
}
}  // namespace

bool EditorCore::setTableCell(const QString& id, int row, int col, const QString& text) {
  const bool done = editTable(*this, elements(), id, [&](El& el) {
    TableData t = tableData(el);
    const QString clean = trimEnd(text);
    if (row < 0 || col < 0 || row >= t.rowCount() || col >= t.colCount() || t.cell(row, col) == clean) return false;
    t.setCell(row, col, clean);
    setTableData(el, t);
    fitTableRows(el);
    return true;
  });
  if (done) ++selectionRev, changed();
  return done;
}

bool EditorCore::changeTableById(const QString& id, const QString& op, int index) {
  const bool done = editTable(*this, elements(), id, [&](El& el) { return changeTable(el, op, index); });
  if (done) ++selectionRev, changed();
  return done;
}

bool EditorCore::setTableSizes(const QString& id, const QVariantList& cols, const QVariantList& rows) {
  const bool done = editTable(*this, elements(), id, [&](El& el) {
    TableData t = tableData(el);
    if ((!cols.isEmpty() && cols.size() != t.colCount()) || (!rows.isEmpty() && rows.size() != t.rowCount())) return false;
    if (!cols.isEmpty()) {
      t.cols.clear();
      for (const QVariant& v : cols) t.cols.push_back(std::max(0.02, v.toDouble()));
    }
    if (!rows.isEmpty()) {
      t.rows.clear();
      for (const QVariant& v : rows) t.rows.push_back(std::max(0.02, v.toDouble()));
    }
    setTableData(el, t);
    fitTableRows(el);
    return true;
  });
  if (done) ++selectionRev, changed();
  return done;
}

bool EditorCore::writeBeside(const QString& value, const QString& font) {
  const auto box = selectionBounds();
  if (!box || value.trimmed().isEmpty()) return false;
  // The colour with the most ink in the selection (else the usual text colour).
  QString color = "#1d1f23";
  size_t most = 0;
  QHash<QString, size_t> ink;
  for (const ElPtr& el : selected().list)
    if (el->kind == Kind::Stroke) ink[el->color] += el->points.size();
  for (auto it = ink.begin(); it != ink.end(); ++it)
    if (it.value() > most) most = it.value(), color = it.key();
  // About the height of one written line of it; typed text: its own size,
  // font and line, so the answer reads as part of it.
  const Els sel = selected().list;
  const El* typed = nullptr;
  for (const ElPtr& el : sel)
    if (el->kind == Kind::Text) typed = el.get();
  const bool onlyText = typed && std::all_of(sel.begin(), sel.end(), [](const ElPtr& e) { return e->kind == Kind::Text; });
  const double lines = std::max(1.0, std::round(box->h / std::max(30.0, box->w / 3)));
  const double size = onlyText ? typed->fontSize.value_or(20) : std::clamp(box->h / lines * 0.62, 14.0, 72.0);
  const QString face = onlyText ? typed->font.value_or(font) : font;
  if (onlyText) color = typed->color.isEmpty() ? color : typed->color;
  const Fit fit = fitTextToBox(value, face, {0, 0, 10000, size * 1.35});
  const double width = std::min(std::max(fit.width + size, 60.0), pageWidth());
  El text;
  text.id = newId();
  text.z = nextZ();
  text.kind = Kind::Text;
  text.text = value.trimmed();
  text.fontSize = size;
  text.color = color;
  text.font = face;
  text.w = width;
  // Beside it, lined up with its middle (typed text: its last line); under it
  // when the page ends first.
  if (box->x + box->w + size * 0.4 + width <= pageWidth()) {
    text.x = box->x + box->w + size * 0.4;
    text.y = onlyText ? typed->y + boxOfEl(*typed).h - size * 1.35 : box->y + box->h / 2 - size * 1.35 / 2;
  } else {
    text.x = std::max(0.0, std::min(box->x, pageWidth() - width));
    text.y = std::min(box->y + box->h + size * 0.3, pageHeight() - size * 1.35);
  }
  const QString id = text.id;
  addElement(makeEl(std::move(text)));
  m_selection = {id};
  ++selectionRev;
  return true;
}

Els EditorCore::selectedInk() {
  Els out;
  for (const ElPtr& el : elements())
    if (m_selection.contains(el->id) && el->kind == Kind::Stroke && el->tool == "pen") out.push_back(el);
  return out;
}

QString EditorCore::selectedText() {
  QStringList out;
  for (const ElPtr& el : elements())
    if (m_selection.contains(el->id)) {
      const QString t = el->toJson()["text"].toString().trimmed();
      if (!t.isEmpty()) out << t;
    }
  return out.join("\n");
}

QVariantList EditorCore::selectedInkStrokes() {
  QVariantList out;
  for (const ElPtr& s : selectedInk()) {
    QVariantList pts;
    for (const Pt& p : s->points) pts << QVariant(QVariantList{p[0], p[1]});
    out << QVariant(pts);
  }
  return out;
}

bool EditorCore::inkToText(const QString& textIn, const QString& font, bool keepInk) {
  const Els ink = selectedInk();
  const QString value = trimmed(textIn);
  if (ink.empty() || value.isEmpty()) return false;
  std::vector<Rect> rs;
  for (const ElPtr& s : ink) rs.push_back(bounds(*s));
  const Rect box = *unionRects(rs);
  const Fit fit = fitTextToBox(value, font, box);
  // The colour with the most ink (the first seen wins a tie).
  std::vector<std::pair<QString, size_t>> counts;
  for (const ElPtr& s : ink) {
    auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == s->color; });
    if (it == counts.end()) counts.push_back({s->color, s->points.size()});
    else it->second += s->points.size();
  }
  std::stable_sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  const int lines = int(value.count('\n')) + 1;
  El text;
  text.id = newId();
  text.z = nextZ();
  text.kind = Kind::Text;
  text.x = box.x;
  text.y = box.y + (box.h - lines * fit.fontSize * 1.35) / 2;
  text.w = std::min(fit.width, std::max(60.0, pageWidth() - box.x));
  text.text = value;
  text.fontSize = fit.fontSize;
  text.color = counts[0].first;
  text.font = font;
  snapshot();
  Els& els = elements();
  if (!keepInk) {
    QSet<QString> gone;
    for (const ElPtr& s : ink) gone.insert(s->id);
    els.erase(std::remove_if(els.begin(), els.end(), [&](const ElPtr& e) { return gone.contains(e->id); }), els.end());
  }
  const QString id = text.id;
  els.push_back(makeEl(std::move(text)));
  m_selection = {id};
  ++selectionRev;
  textFont = font;
  changed();
  return true;
}

// ------------------------------------------------------------ text editing

void EditorCore::startTextEdit(const ElPtr& el_, bool isNew) {
  finishTextEdit();
  m_editingBefore = elements();
  snapshot();
  ElPtr el = el_;
  if (isNew) {
    elements().push_back(el);
  } else {
    // Typing changes a copy, so the history keeps the text as it was.
    ElPtr live = copyOf(*el);
    for (ElPtr& x : elements())
      if (x == el) x = live;
    el = live;
  }
  m_editing = el;
  m_editingNew = isNew;
  m_hidden.insert(el->id);
  ++rev;
  touchOverlay();
}

void EditorCore::updateEdit(const QString& text) {
  El* el = m_editing.get();
  if (!el) return;
  // A new text box opens to the right of the tap; Arabic flips it to the left.
  if (m_editingNew && el->kind == Kind::Text && el->text.isEmpty() && hasLetter(text) && draw::isRtl(text)) el->x = std::max(0.0, el->x - el->w);
  el->text = text;
  el->cachedBounds.reset();
  el->cachedFrame.reset();
}

QVariantMap continueListFor(const QString& text, int caret) {
  const auto e = continueList(text, caret);
  if (!e) return {};
  return {{"value", e->value}, {"caret", e->caret}};
}

void EditorCore::applyFormat(const QVariantMap& change) {
  const bool hasBold = change.contains("bold") && !change.value("bold").isNull();
  const bool hasItalic = change.contains("italic") && !change.value("italic").isNull();
  const QString align = change.value("align").toString();
  const QString font = change.value("font").toString();
  if (hasBold) textFormat.bold = change.value("bold").toBool();
  if (hasItalic) textFormat.italic = change.value("italic").toBool();
  if (!align.isEmpty()) textFormat.align = align;
  if (!font.isEmpty()) textFont = font;
  El* el = m_editing.get();
  if (!el) return;
  if (hasBold) el->bold = change.value("bold").toBool();
  if (hasItalic) el->italic = change.value("italic").toBool();
  if (!align.isEmpty()) el->align = align;
  if (!change.value("list").toString().isEmpty()) el->text = toggleList(el->text, change.value("list").toString());
  if (!font.isEmpty()) el->font = font;
  if (const double fs = change.value("fontSize").toDouble(); fs != 0) el->fontSize = fs;
  if (!change.value("color").toString().isEmpty()) el->color = change.value("color").toString();
  el->cachedBounds.reset();
  el->cachedFrame.reset();
}

void EditorCore::finishTextEdit() {
  if (!m_editing) return;
  ElPtr el = m_editing;
  m_editing = nullptr;
  el->text = trimEnd(el->text);
  el->cachedBounds.reset();
  el->cachedFrame.reset();
  m_hidden.remove(el->id);
  Els& els = elements();
  if (el->kind == Kind::Text && trimmed(el->text).isEmpty()) {
    els.erase(std::remove_if(els.begin(), els.end(), [&](const ElPtr& x) { return x->id == el->id; }), els.end());
  } else if (el->kind == Kind::Text) {
    const double natural = std::ceil(draw::naturalTextWidth(el->toJson()) + 4);
    if (natural < el->w) {
      El shrunk = *el;
      shrunk.w = std::max(24.0, natural);
      const QString a = el->align.value_or(QString());
      const bool rightAligned = draw::isRtl(el->text) ? a != "right" && a != "center" : a == "right";
      if (a == "center") shrunk.x = el->x + (el->w - shrunk.w) / 2;
      else if (rightAligned) shrunk.x = el->x + el->w - shrunk.w;
      ElPtr s = makeEl(std::move(shrunk));
      for (ElPtr& x : els)
        if (x->id == el->id) x = s;
    }
  }
  // A copy, so the canvas sees a changed element.
  for (ElPtr& x : els)
    if (x->id == el->id) x = copyOf(*x);
  // Nothing changed: no history step.
  const Els& before = m_editingBefore;
  bool same = before.size() == els.size();
  for (size_t k = 0; same && k < els.size(); ++k) {
    const ElPtr& a = els[k];
    const ElPtr& b = before[k];
    same = a == b || (a->id == b->id && a->toJson() == b->toJson());
  }
  m_editingBefore.clear();
  if (same && !m_undo.empty()) m_undo.pop_back();
  changed();
}

// ------------------------------------------------------------ gestures

bool EditorCore::toggleTapeAt(double x, double y) {
  Els& els = elements();
  for (auto it = els.rbegin(); it != els.rend(); ++it) {
    const El& e = **it;
    if (e.kind != Kind::Stroke || e.style.value_or(QString()) != "tape" || e.points.empty()) continue;
    const Pt a = e.points.front(), b = e.points.back();
    if (ink::segmentDistance(QPointF(x, y), QPointF(a[0], a[1]), QPointF(b[0], b[1])) > e.width / 2 + 2 / zoom) continue;
    El next = e;
    if (next.extra["peek"].toBool()) next.extra.remove("peek");
    else next.extra.insert("peek", true);
    *it = makeEl(std::move(next));
    changed();
    return true;
  }
  return false;
}

bool EditorCore::scratchAway(const std::vector<Pt>& points) {
  if (!scratchOut) return false;
  const auto scribble = asScribble(points);
  if (!scribble) return false;
  std::vector<const El*> list;
  for (const ElPtr& e : elements())
    if (!m_hidden.contains(e->id)) list.push_back(e.get());
  const QSet<QString> gone = scratchedOut(*scribble, list);
  // Over nothing: a scribble drawn on purpose, kept as ink.
  if (gone.isEmpty()) return false;
  snapshot();
  Els& els = elements();
  m_faded.clear();
  for (const ElPtr& e : els)
    if (gone.contains(e->id)) m_faded.push_back(e);
  els.erase(std::remove_if(els.begin(), els.end(), [&](const ElPtr& e) { return gone.contains(e->id); }), els.end());
  m_recentInk.erase(std::remove_if(m_recentInk.begin(), m_recentInk.end(), [&](const Recent& r) { return gone.contains(r.id); }), m_recentInk.end());
  if (std::any_of(gone.begin(), gone.end(), [&](const QString& id) { return m_selection.contains(id); })) {
    m_selection.subtract(gone);
    ++selectionRev;
  }
  fadeLevel = 1;
  ++scratches;
  touchOverlay();
  changed();
  return true;
}

// ------------------------------------------------------------ drawing

void EditorCore::drawOverlay(Canvas& c, int pi) {
  if (pi != active) return;
  // Ink just scratched out, fading away.
  if (fadeLevel > 0 && !m_faded.empty()) {
    c.save();
    c.setGlobalAlpha(std::clamp(fadeLevel, 0.0, 1.0) * 0.85);
    for (const ElPtr& e : m_faded) draw::drawElement(c, e->toJson());
    c.restore();
  }
  const Gesture& g = m_g;
  if (g.type == G::Draw) {
    if (g.preview && g.tool == "highlighter" && g.preview->shape.kind == "line") {
      draw::drawStroke(c, straightHighlight(g.preview->shape.points).toJson());
    } else if (g.preview) {
      if (g.tool == "pen" && keepsPen(penStyle))
        for (const El& el : penStrokes(*g.preview)) draw::drawStroke(c, el.toJson());
      else draw::drawElement(c, g.preview->toJson());
      if (g.held) drawHeldHint(c, *g.preview, g.held->mode);
    } else if (g.ruler) {
      El live;
      live.id = "live";
      live.kind = Kind::Stroke;
      live.tool = g.tool == "highlighter" ? "highlighter" : "pen";
      if (g.tool == "pen") live.style = penStyle;
      live.color = colors.value(g.tool);
      live.width = widths.value(g.tool);
      live.points = g.points;
      live.smooth = 0;
      draw::drawStroke(c, live.toJson(), true);
    }
  } else if (g.type == G::Shape) {
    if (g.preview) draw::drawElement(c, g.preview->toJson());
  } else if (g.type == G::Lasso) {
    drawLasso(c, g.lasso, zoom);
  } else if (g.type == G::Move) {
    drawGuides(c, g.guides, zoom);
  } else if (g.type == G::Handle) {
    draw::drawElement(c, g.preview->toJson());
  } else if (g.type == G::Transform) {
    for (const ElPtr& el : g.previews) draw::drawElement(c, el->toJson());
    drawTransformedBox(c, transformOutline(), zoom);
  }
  std::optional<Rect> sel = g.type == G::Handle ? std::optional<Rect>(bounds(*g.preview)) : g.type == G::Transform ? std::nullopt : selectionBounds();
  if (sel) {
    Rect r = *sel;
    if (g.type == G::Move) r.x += g.dx, r.y += g.dy;
    drawSelection(c, r, zoom);
  }
  const ElPtr segment = g.type == G::Handle ? g.preview : g.type != G::None ? nullptr : singleSegment();
  if (segment) {
    drawHandles(c, *segment, zoom);
  } else if (g.type == G::None) {
    if (const auto box = transformBox()) drawGroupHandles(c, *box, zoom);
  }
}

El EditorCore::transformed(const El& el, const Gesture& g) const {
  // Text alone, pulled by a side: its wrap width changes by as much as the
  // frame did (the frame fits the letters, so a ratio of the frame would
  // stretch the wider wrap box further than the pointer went), and the
  // other side stays where it is.
  const bool side = (g.handle == "e" || g.handle == "w") && !g.angle && std::abs(g.sy - 1) < 1e-9;
  const bool allText = std::all_of(g.original.begin(), g.original.end(), [](const ElPtr& e) { return e->kind == Kind::Text; });
  if (el.kind == Kind::Text && side && allText && !el.rot()) {
    El out = el;
    if (g.original.size() == 1) {
      // One text box: it wraps to the frame being dragged (the frame is a few
      // units around the letters), so the pulled edge stays under the pointer.
      const P2 a = applyAffine(g.m, {g.box.x, g.box.y}), b = applyAffine(g.m, {g.box.x + g.box.w, g.box.y});
      const double left = std::min(a[0], b[0]), right = std::max(a[0], b[0]);
      out.w = std::max(40.0, right - left - 8);
      out.x = g.handle == "w" ? right - 4 - out.w : left + 4;
      return out;
    }
    const double dw = g.box.w * (g.sx - 1);
    out.w = std::max(40.0, el.w + dw);
    if (g.handle == "w") out.x = el.x - (out.w - el.w);
    return out;
  }
  return transformElement(el, g.m, g.sx, g.sy, g.angle);
}

std::vector<P2> EditorCore::transformOutline() const {
  const Gesture& g = m_g;
  if (!g.previews.empty() && g.previews.size() == g.original.size() && !g.angle) {
    std::vector<Rect> rs;
    // Around where the text will be: the same fit as the selection frame;
    // pulled by a side, the width it wraps to as well (that's what changes).
    const bool side = g.handle == "e" || g.handle == "w";
    for (const ElPtr& el : g.previews) rs.push_back(frameOfEl(*el));
    // Pulled by a side: the frame being dragged too (the width it wraps to).
    if (side) {
      std::vector<P2> box = {{g.box.x, g.box.y}, {g.box.x + g.box.w, g.box.y + g.box.h}};
      for (P2& p : box) p = applyAffine(g.m, p);
      rs.push_back(rectOf(box));
    }
    if (const auto u = unionRects(rs)) {
      const Rect b = *u;
      return {{b.x, b.y}, {b.x + b.w, b.y}, {b.x + b.w, b.y + b.h}, {b.x, b.y + b.h}};
    }
  }
  const Rect& b = g.box;
  std::vector<P2> out = {{b.x, b.y}, {b.x + b.w, b.y}, {b.x + b.w, b.y + b.h}, {b.x, b.y + b.h}};
  for (P2& p : out) p = applyAffine(g.m, p);
  return out;
}

std::optional<Rect> EditorCore::overlayBounds() {
  const Gesture& g = m_g;
  std::vector<Rect> parts;
  const auto sel = selectionBounds();
  if (sel) parts.push_back(*sel);
  if (g.type == G::Draw && (g.preview || g.ruler)) {
    if (g.preview) parts.push_back(bounds(*g.preview));
    if (!g.points.empty()) parts.push_back(rectOf(flat(g.points)));
  }
  if (g.type == G::Shape && g.preview) parts.push_back(bounds(*g.preview));
  if (g.type == G::Lasso) parts.push_back(rectOf(g.lasso));
  // Ink just scratched out, while it fades.
  if (fadeLevel > 0)
    for (const ElPtr& el : m_faded) parts.push_back(bounds(*el));
  if (g.type == G::Handle) parts.push_back(bounds(*g.preview));
  if (g.type == G::Move && sel) parts.push_back({sel->x + g.dx, sel->y + g.dy, sel->w, sel->h});
  if (g.type == G::Move && !g.guides.empty()) parts.push_back({0, 0, pageWidth(), pageHeight()});
  if (g.type == G::Transform) {
    for (const ElPtr& el : g.previews) parts.push_back(bounds(*el)), parts.push_back(frameOfEl(*el));
    parts.push_back(rectOf(transformOutline()));
  }
  const auto u = unionRects(parts);
  if (!u) return std::nullopt;
  // Room for handles, the rotation knob, guides' labels and the held hint.
  return pad(*u, 48 / zoom);
}

void EditorCore::drawHeldHint(Canvas& c, const El& el, const QString& mode) const {
  const QString text = mode == "transform" ? QStringLiteral("Rotate & resize · hold still to move") : QStringLiteral("Move · hold still to rotate & resize");
  const Rect r = bounds(el);
  const double z = zoom;
  c.save();
  c.setFont(draw::jsNumber(12 / z) + "px system-ui, sans-serif");
  const double w = c.textWidth(text) + 16 / z;
  const double hgt = 22 / z;
  const double x = std::max(4 / z, std::min(pageWidth() - w - 4 / z, r.x + r.w / 2 - w / 2));
  const double y = std::max(4 / z, r.y - hgt - 10 / z);
  c.setFill(QStringLiteral("rgba(17, 24, 39, 0.85)"));
  c.beginPath();
  c.roundRect(x, y, w, hgt, QJSValue(6 / z));
  c.fill();
  c.setFill(QStringLiteral("#fff"));
  c.setTextBaseline("middle");
  c.fillText(text, x + 8 / z, y + hgt / 2);
  c.restore();
}

// ------------------------------------------------------------ helpers

Fit fitTextToBox(const QString& text, const QString& font, const Rect& box) {
  const int lines = std::max(1, int(text.split('\n').size()));
  const double perUnit = draw::measureText(text, font, 100) / 100;
  const double byWidth = perUnit > 0 ? box.w / perUnit : box.h;
  const double byHeight = (box.h / lines) * 1.15;
  const double fontSize = std::floor(std::min(160.0, std::max(10.0, std::min(byWidth, byHeight * 1.6))) + 0.5);
  return {fontSize, std::ceil(draw::measureText(text, font, fontSize) + fontSize * 0.4)};
}

std::vector<Pt> thinPoints(const std::vector<Pt>& points, double min) {
  if (points.size() < 3) return points;
  std::vector<Pt> out{points[0]};
  for (size_t i = 1; i + 1 < points.size(); ++i) {
    const Pt& last = out.back();
    if (std::hypot(points[i][0] - last[0], points[i][1] - last[1]) >= min) out.push_back(points[i]);
  }
  out.push_back(points.back());
  return out;
}

void drawSelection(Canvas& c, const Rect& r, double zoom) {
  c.save();
  c.setStroke(QStringLiteral("#2563eb"));
  c.setLineWidth(1.5 / zoom);
  c.setDash({6 / zoom, 4 / zoom});
  c.strokeRect(r.x - 6, r.y - 6, r.w + 12, r.h + 12);
  c.restore();
}

void drawGroupHandles(Canvas& c, const Rect& box, double zoom) {
  const double size = 9 / zoom;
  const std::vector<Handle> hs = groupHandles(box, zoom);
  const Handle rot = *std::find_if(hs.begin(), hs.end(), [](const Handle& h) { return h.id == "rot"; });
  c.save();
  c.setLineWidth(1.5 / zoom);
  c.setStroke(QStringLiteral("#2563eb"));
  c.setFill(QStringLiteral("#ffffff"));
  c.beginPath();
  c.moveTo(rot.x, box.y + box.h);
  c.lineTo(rot.x, rot.y - size * 0.7);
  c.stroke();
  for (const Handle& h : hs) {
    c.beginPath();
    // Handles with the app's small corners (the rotation knob a little larger).
    if (h.id == "rot") c.roundRect(h.x - size * 0.7, h.y - size * 0.7, size * 1.4, size * 1.4, QJSValue(size * 0.35));
    else c.roundRect(h.x - size / 2, h.y - size / 2, size, size, QJSValue(size * 0.25));
    c.fill();
    c.stroke();
  }
  // A small turning arrow inside the rotation knob.
  c.beginPath();
  c.arc(rot.x, rot.y, size * 0.35, -M_PI * 0.9, M_PI * 0.5);
  c.stroke();
  c.restore();
}

void drawTransformedBox(Canvas& c, const std::vector<P2>& corners, double zoom) {
  c.save();
  c.setStroke(QStringLiteral("#2563eb"));
  c.setLineWidth(1.5 / zoom);
  c.setDash({6 / zoom, 4 / zoom});
  c.beginPath();
  for (size_t i = 0; i < corners.size(); ++i) {
    if (i) c.lineTo(corners[i][0], corners[i][1]);
    else c.moveTo(corners[i][0], corners[i][1]);
  }
  c.closePath();
  c.stroke();
  c.restore();
}

void drawHandles(Canvas& c, const El& el, double zoom) {
  const double size = 9 / zoom;
  c.save();
  c.setLineWidth(1.5 / zoom);
  c.setStroke(QStringLiteral("#2563eb"));
  c.setFill(QStringLiteral("#ffffff"));
  for (const Handle& h : handlesFor(el)) {
    c.beginPath();
    if (h.id == "p0" || h.id == "p1") c.roundRect(h.x - size * 0.6, h.y - size * 0.6, size * 1.2, size * 1.2, QJSValue(size * 0.3));
    else c.roundRect(h.x - size / 2, h.y - size / 2, size, size, QJSValue(size * 0.25));
    c.fill();
    c.stroke();
  }
  c.restore();
}

void drawLasso(Canvas& c, const std::vector<P2>& pts, double zoom) {
  if (pts.size() < 2) return;
  c.save();
  c.setStroke(QStringLiteral("#2563eb"));
  c.setFill(QStringLiteral("rgba(37, 99, 235, 0.06)"));
  c.setLineWidth(1.5 / zoom);
  c.setDash({5 / zoom, 5 / zoom});
  c.beginPath();
  c.moveTo(pts[0][0], pts[0][1]);
  for (size_t i = 1; i < pts.size(); ++i) c.lineTo(pts[i][0], pts[i][1]);
  c.closePath();
  c.fill();
  c.stroke();
  c.restore();
}

void drawGuides(Canvas& c, const std::vector<Guide>& guides, double zoom) {
  if (guides.empty()) return;
  c.save();
  c.setStroke(QStringLiteral("#db2777"));
  c.setFill(QStringLiteral("#db2777"));
  c.setLineWidth(1.25 / zoom);
  c.setFont(draw::jsNumber(11 / zoom) + "px system-ui, sans-serif");
  c.setTextBaseline("bottom");
  for (const Guide& g : guides) {
    c.beginPath();
    if (g.axis == 'y') {
      c.moveTo(g.from - 12 / zoom, g.value);
      c.lineTo(g.to + 12 / zoom, g.value);
    } else {
      c.moveTo(g.value, g.from - 12 / zoom);
      c.lineTo(g.value, g.to + 12 / zoom);
    }
    c.stroke();
    const double lx = g.labelX;
    const double ly = g.axis == 'y' ? g.labelY - 3 / zoom : g.labelY;
    const double tw = c.textWidth(g.label);
    c.save();
    c.setFill(QStringLiteral("rgba(255,255,255,0.9)"));
    c.fillRect(lx - 2 / zoom, ly - 13 / zoom, tw + 4 / zoom, 14 / zoom);
    c.restore();
    c.fillText(g.label, lx, ly);
  }
  c.restore();
}

}  // namespace ed
