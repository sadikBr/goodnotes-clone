#include "InkCanvas.h"

#include <QCursor>
#include <QPainter>
#include <QPixmap>
#include <QJsonDocument>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>
#include <QTabletEvent>
#include <QUuid>
#include <algorithm>
#include <cmath>

#include "Core.h"
#include "Paper.h"

namespace {
constexpr float kPageGap = 28;     // page units between pages
constexpr qreal kPad = 24;         // screen pixels around the pages
constexpr qreal kMinZoom = 0.25, kMaxZoom = 4;
constexpr float kLazyPen = 1.1f;   // screen pixels the ink may trail the pen (takes out tremor and pixel steps)

double nowMs() {
  static QElapsedTimer t = [] {
    QElapsedTimer e;
    e.start();
    return e;
  }();
  return double(t.nsecsElapsed()) / 1e6;
}

QSGGeometryNode* meshNode(const std::vector<QPointF>& tris, const QColor& color) {
  auto* g = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), int(tris.size()));
  g->setDrawingMode(QSGGeometry::DrawTriangles);
  QSGGeometry::Point2D* v = g->vertexDataAsPoint2D();
  for (size_t i = 0; i < tris.size(); ++i) v[i].set(float(tris[i].x()), float(tris[i].y()));
  auto* m = new QSGFlatColorMaterial;
  m->setColor(color);
  auto* n = new QSGGeometryNode;
  n->setGeometry(g);
  n->setMaterial(m);
  n->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
  return n;
}

void setMesh(QSGGeometryNode* n, const std::vector<QPointF>& tris, const QColor& color) {
  QSGGeometry* g = n->geometry();
  g->allocate(int(tris.size()));
  QSGGeometry::Point2D* v = g->vertexDataAsPoint2D();
  for (size_t i = 0; i < tris.size(); ++i) v[i].set(float(tris[i].x()), float(tris[i].y()));
  n->markDirty(QSGNode::DirtyGeometry);
  auto* m = static_cast<QSGFlatColorMaterial*>(n->material());
  if (m->color() != color) {
    m->setColor(color);
    n->markDirty(QSGNode::DirtyMaterial);
  }
}

QColor strokeColor(const QColor& c, ink::Pen pen) {
  if (pen != ink::Pen::Highlighter) return c;
  QColor h = c;
  h.setAlphaF(0.35f);
  return h;
}

double round2(double v) { return std::round(v * 100) / 100; }
}  // namespace

// --- strokes --------------------------------------------------------------------

void InkCanvas::Stroke::build() {
  mesh = std::make_shared<ink::Mesh>();
  ink::strokeMesh(pts, pen, width, smooth, *mesh);
  if (pts.empty()) {
    bounds = {};
    return;
  }
  float x0 = pts[0].x, x1 = x0, y0 = pts[0].y, y1 = y0;
  for (const auto& p : pts) {
    x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
  }
  const float r = width;
  bounds = QRectF(x0 - r, y0 - r, x1 - x0 + 2 * r, y1 - y0 + 2 * r);
}

QJsonObject InkCanvas::storedOf(const Stroke& s) {
  QJsonObject data = s.data;
  QJsonArray pts;
  for (const auto& p : s.pts) pts.append(QJsonArray{round2(p.x), round2(p.y), std::round(p.p * 1000) / 1000});
  data["id"] = s.id;
  data["z"] = s.z;
  data["kind"] = "stroke";
  data["tool"] = s.pen == ink::Pen::Highlighter ? "highlighter" : "pen";
  if (s.pen != ink::Pen::Highlighter) data["style"] = ink::penName(s.pen);
  data["color"] = s.color.name(QColor::HexRgb);
  data["width"] = s.width;
  data["points"] = pts;
  if (s.pen != ink::Pen::Highlighter) data["smooth"] = s.smooth;
  return QJsonObject{{"id", s.id}, {"kind", "stroke"}, {"z", s.z}, {"data", data}};
}

// --- setup ------------------------------------------------------------------------

InkCanvas::InkCanvas(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptTouchEvents(true);
  setAcceptHoverEvents(true);
  updateCursor();
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(600);
  connect(&m_saveTimer, &QTimer::timeout, this, &InkCanvas::saveNow);
}

InkCanvas::~InkCanvas() { saveNow(); }

void InkCanvas::itemChange(ItemChange change, const ItemChangeData& value) {
  if (change == ItemSceneChange) {
    // Tablet events reach the window first: take the pen's there, with its pressure.
    if (m_window) m_window->removeEventFilter(this);
    m_window = value.window;
    if (m_window) m_window->installEventFilter(this);
  }
  QQuickItem::itemChange(change, value);
}

void InkCanvas::setNotebookId(const QString& id) {
  if (id == m_notebookId) return;
  saveNow();
  m_notebookId = id;
  emit notebookIdChanged();
  loadNotebook();
}

void InkCanvas::setTool(const QString& t) {
  if (t == m_tool) return;
  m_tool = t;
  updateCursor();
  emit toolChanged();
  refresh();
}

void InkCanvas::setPenCursor(const QString& c) {
  if (c == m_penCursor) return;
  m_penCursor = c;
  updateCursor();
  emit settingsChanged();
}

void InkCanvas::updateCursor() {
  // The eraser draws its own ring.
  if (m_tool == "eraser" || m_penCursor == "none") return setCursor(Qt::BlankCursor);
  if (m_penCursor == "dot") {
    static const QCursor dot = [] {
      QPixmap pm(12, 12);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(QPen(QColor(255, 255, 255, 230), 1.5));
      p.setBrush(QColor(20, 20, 20));
      p.drawEllipse(QPointF(6, 6), 3, 3);
      return QCursor(pm, 6, 6);
    }();
    return setCursor(dot);
  }
  setCursor(Qt::CrossCursor);
}

float InkCanvas::curved(qreal pressure) const {
  const qreal gamma = m_pressureCurve == "light" ? 0.6 : m_pressureCurve == "firm" ? 1.6 : 1.0;
  return float(std::pow(std::clamp<qreal>(pressure, 0, 1), gamma));
}
void InkCanvas::setPenStyle(const QString& s) {
  const ink::Pen p = ink::penFromName(s, false);
  if (p == m_penStyle) return;
  m_penStyle = p;
  emit penStyleChanged();
}
void InkCanvas::setPenColor(const QColor& c) {
  if (c == m_penColor) return;
  m_penColor = c;
  emit penColorChanged();
}
void InkCanvas::setHighlighterColor(const QColor& c) {
  if (c == m_hlColor) return;
  m_hlColor = c;
  emit highlighterColorChanged();
}
void InkCanvas::setPenWidth(qreal w) {
  if (qFuzzyCompare(w, m_penWidth)) return;
  m_penWidth = w;
  emit penWidthChanged();
}
void InkCanvas::setHighlighterWidth(qreal w) {
  if (qFuzzyCompare(w, m_hlWidth)) return;
  m_hlWidth = w;
  emit highlighterWidthChanged();
}
void InkCanvas::setEraserWidth(qreal w) {
  if (qFuzzyCompare(w, m_eraserWidth)) return;
  m_eraserWidth = w;
  emit eraserWidthChanged();
}
void InkCanvas::setSmoothing(qreal s) {
  if (qFuzzyCompare(s + 1, m_smoothing + 1)) return;
  m_smoothing = s;
  emit smoothingChanged();
}
void InkCanvas::setPenOnly(bool on) {
  if (on == m_penOnly) return;
  m_penOnly = on;
  emit penOnlyChanged();
}

// --- loading and saving --------------------------------------------------------------

void InkCanvas::loadNotebook() {
  m_pages.clear();
  m_undo.clear();
  m_redo.clear();
  m_g = Gesture();
  m_scroll = {};
  m_fitPending = true;
  if (!m_notebookId.isEmpty()) {
    QString err;
    const QJsonArray pages = InkleafCore::instance()->callJson("pages", {{"notebookId", m_notebookId}}, &err).toArray();
    if (!err.isEmpty()) emit error(err);
    for (const auto& v : pages) {
      const QJsonObject o = v.toObject();
      Page p;
      p.id = o["id"].toString();
      p.w = float(o["width"].toDouble(794));
      p.h = float(o["height"].toDouble(1123));
      p.tmpl = o["template"].toString("blank");
      p.paper = QColor(o["paperColor"].toString("#fffdf7"));
      m_pages.push_back(std::move(p));
    }
  }
  layoutPages();
  if (width() > 0) fitWidth();
  m_rebuildAll = true;
  emit pagesChanged();
  emit historyChanged();
  emit viewChanged();
  refresh();
}

void InkCanvas::ensureLoaded(int i) {
  if (i < 0 || i >= int(m_pages.size()) || m_pages[i].loaded) return;
  Page& p = m_pages[i];
  p.loaded = true;
  QString err;
  const QJsonArray rows = InkleafCore::instance()->callJson("elements", {{"pageId", p.id}}, &err).toArray();
  if (!err.isEmpty()) emit error(err);
  for (const auto& v : rows) {
    const QJsonObject row = v.toObject();
    if (row["kind"].toString() != "stroke") {
      p.others.append(row);
      continue;
    }
    const QJsonObject d = row["data"].toObject();
    Stroke s;
    s.id = row["id"].toString();
    s.z = row["z"].toInt();
    s.data = d;
    s.pen = ink::penFromName(d["style"].toString(), d["tool"].toString() == "highlighter");
    s.color = QColor(d["color"].toString("#111827"));
    s.width = float(d["width"].toDouble(3));
    s.smooth = float(d["smooth"].toDouble(ink::kSmoothNormal));
    for (const auto& pv : d["points"].toArray()) {
      const QJsonArray a = pv.toArray();
      s.pts.push_back({float(a.at(0).toDouble()), float(a.at(1).toDouble()), float(a.at(2).toDouble(0.5))});
    }
    s.build();
    p.strokes.push_back(std::move(s));
  }
  std::sort(p.strokes.begin(), p.strokes.end(), [](const Stroke& a, const Stroke& b) { return a.z < b.z; });
  p.version++;
  emit pagesChanged();
}

void InkCanvas::markDirty(int i) {
  m_pages[i].dirty = true;
  m_pages[i].version++;
  m_saveTimer.start();
  refresh();
}

void InkCanvas::saveNow() {
  m_saveTimer.stop();
  for (Page& p : m_pages) {
    if (!p.dirty) continue;
    QJsonArray rows = p.others;
    for (const Stroke& s : p.strokes) rows.append(storedOf(s));
    QString err;
    InkleafCore::instance()->callJson("saveElements", {{"pageId", p.id}, {"elements", rows}}, &err);
    if (err.isEmpty()) p.dirty = false;
    else emit error(err);
  }
}

void InkCanvas::flush() { saveNow(); }

void InkCanvas::addPage() {
  saveNow();
  QString err;
  InkleafCore::instance()->callJson("addPage", {{"notebookId", m_notebookId}, {"index", QJsonValue()}, {"template", QJsonValue()}}, &err);
  if (!err.isEmpty()) return emit error(err);
  // Keep what's loaded; add the new page from the fresh list.
  const QJsonArray pages = InkleafCore::instance()->callJson("pages", {{"notebookId", m_notebookId}}).toArray();
  for (const auto& v : pages) {
    const QJsonObject o = v.toObject();
    const QString id = o["id"].toString();
    if (std::any_of(m_pages.begin(), m_pages.end(), [&](const Page& p) { return p.id == id; })) continue;
    Page p;
    p.id = id;
    p.w = float(o["width"].toDouble(794));
    p.h = float(o["height"].toDouble(1123));
    p.tmpl = o["template"].toString("blank");
    p.paper = QColor(o["paperColor"].toString("#fffdf7"));
    m_pages.push_back(std::move(p));
  }
  layoutPages();
  m_rebuildAll = true;
  emit pagesChanged();
  goToPage(int(m_pages.size()) - 1);
}

// --- layout and view ---------------------------------------------------------------

void InkCanvas::layoutPages() {
  float top = 0;
  m_maxPageW = 1;
  for (Page& p : m_pages) {
    p.top = top;
    top += p.h + kPageGap;
    m_maxPageW = std::max(m_maxPageW, p.w);
  }
  m_contentH = m_pages.empty() ? 0 : top - kPageGap;
}

void InkCanvas::geometryChange(const QRectF& n, const QRectF& o) {
  QQuickItem::geometryChange(n, o);
  if (m_fitPending && n.width() > 0 && !m_pages.empty()) fitWidth();
  clampScroll();
  refresh();
}

void InkCanvas::clampScroll() {
  const qreal maxX = std::max<qreal>(0, m_maxPageW * m_zoom + 2 * kPad - width());
  const qreal maxY = std::max<qreal>(0, m_contentH * m_zoom + 2 * kPad - height());
  m_scroll.setX(std::clamp<qreal>(m_scroll.x(), 0, maxX));
  m_scroll.setY(std::clamp<qreal>(m_scroll.y(), 0, maxY));
}

/// Where page-unit content x=0 of the widest page sits on screen.
static QPointF originFor(qreal itemW, qreal contentW, QPointF scroll) {
  const qreal x = contentW + 2 * kPad <= itemW ? (itemW - contentW) / 2 : kPad - scroll.x();
  return {x, kPad - scroll.y()};
}

QPointF InkCanvas::toPage(QPointF s, int* pageIndex) const {
  const QPointF o = originFor(width(), m_maxPageW * m_zoom, m_scroll);
  const QPointF c((s.x() - o.x()) / m_zoom, (s.y() - o.y()) / m_zoom);  // content units
  int best = -1;
  for (int i = 0; i < int(m_pages.size()); ++i) {
    const Page& p = m_pages[i];
    if (c.y() >= p.top - kPageGap / 2 && c.y() <= p.top + p.h + kPageGap / 2) {
      best = i;
      break;
    }
  }
  if (pageIndex) *pageIndex = best;
  if (best < 0) return c;
  const Page& p = m_pages[best];
  return {c.x() - (m_maxPageW - p.w) / 2, c.y() - p.top};
}

int InkCanvas::pageAt(QPointF s) const {
  int i = -1;
  const QPointF p = toPage(s, &i);
  if (i < 0) return -1;
  const Page& pg = m_pages[i];
  return (p.x() >= 0 && p.y() >= 0 && p.x() <= pg.w && p.y() <= pg.h) ? i : -1;
}

int InkCanvas::currentPage() const {
  int i = -1;
  toPage(QPointF(width() / 2, height() / 2), &i);
  return std::max(0, i);
}

void InkCanvas::zoomAt(qreal z, QPointF anchor) {
  z = std::clamp(z, kMinZoom, kMaxZoom);
  const QPointF o = originFor(width(), m_maxPageW * m_zoom, m_scroll);
  const QPointF content((anchor.x() - o.x()) / m_zoom, (anchor.y() - o.y()) / m_zoom);
  m_zoom = z;
  // Keep the content under the anchor where it is.
  m_scroll.setX(kPad + content.x() * m_zoom - anchor.x());
  m_scroll.setY(kPad + content.y() * m_zoom - anchor.y());
  clampScroll();
  m_rebuildAll = true;  // the eraser ring and hairlines follow the zoom
  emit viewChanged();
  refresh();
}

void InkCanvas::zoomBy(qreal f) { zoomAt(m_zoom * f, QPointF(width() / 2, height() / 2)); }

void InkCanvas::fitWidth() {
  if (width() <= 0 || m_pages.empty()) return;
  m_fitPending = false;
  const qreal z = std::min<qreal>(1.6, (width() - 2 * kPad - 16) / m_maxPageW);
  const qreal topContent = (m_scroll.y() - 0) / m_zoom;
  m_zoom = std::clamp(z, kMinZoom, kMaxZoom);
  m_scroll = QPointF(0, topContent * m_zoom);
  clampScroll();
  emit viewChanged();
  refresh();
}

void InkCanvas::goToPage(int i) {
  if (i < 0 || i >= int(m_pages.size())) return;
  m_scroll.setY(m_pages[i].top * m_zoom);
  clampScroll();
  emit viewChanged();
  refresh();
}

int InkCanvas::hiddenElements() const {
  int n = 0;
  for (const Page& p : m_pages) n += p.others.size();
  return n;
}

// --- drawing -------------------------------------------------------------------------

bool InkCanvas::isPenOnlyBlocked(bool stylus) const { return m_penOnly && !stylus; }

void InkCanvas::begin(QPointF s, float pressure, double t, bool stylus, bool eraserEnd) {
  const int pi = pageAt(s);
  if (pi < 0) return;
  ensureLoaded(pi);
  int dummy;
  const QPointF p = toPage(s, &dummy);
  m_g = Gesture();
  m_g.page = pi;
  m_g.lastScreen = s;
  const bool erase = eraserEnd || m_tool == "eraser";
  if (erase) {
    m_g.kind = Gesture::Erase;
    m_hover = s;
    eraseAt(pi, p);
    refresh();
    return;
  }
  m_g.kind = Gesture::Draw;
  Stroke& st = m_g.live;
  const bool hl = m_tool == "highlighter";
  st.pen = hl ? ink::Pen::Highlighter : m_penStyle;
  st.color = hl ? m_hlColor : m_penColor;
  st.width = float(hl ? m_hlWidth : m_penWidth);
  st.smooth = float(m_smoothing);
  const float pr = ink::speedSensitive(st.pen) ? m_g.speed.next(float(p.x()), float(p.y()), t, stylus ? pressure : -1.f) : (stylus ? pressure : 0.5f);
  st.pts.push_back({float(p.x()), float(p.y()), pr});
  st.build();
  m_liveChanged = true;
  refresh();
}

void InkCanvas::extend(QPointF s, float pressure, double t, bool stylus) {
  if (m_g.kind == Gesture::None) return;
  Page& pg = m_pages[m_g.page];
  const QPointF o = originFor(width(), m_maxPageW * m_zoom, m_scroll);
  QPointF p((s.x() - o.x()) / m_zoom - (m_maxPageW - pg.w) / 2, (s.y() - o.y()) / m_zoom - pg.top);
  if (m_g.kind == Gesture::Erase) {
    m_hover = s;
    eraseAt(m_g.page, p);
    // Redraw on every move, not only when something was erased: the
    // eraser ring follows the pointer.
    refresh();
    return;
  }
  Stroke& st = m_g.live;
  const ink::Point& last = st.pts.back();
  // Lazy pen: the ink trails the tip by at most about a screen pixel.
  const float lazy = float(kLazyPen / m_zoom);
  const float d = float(std::hypot(p.x() - last.x, p.y() - last.y));
  if (d <= lazy) return;
  p = QPointF(last.x + (p.x() - last.x) * (d - lazy) / d, last.y + (p.y() - last.y) * (d - lazy) / d);
  const float pr = ink::speedSensitive(st.pen) ? m_g.speed.next(float(p.x()), float(p.y()), t, stylus ? pressure : -1.f) : (stylus ? pressure : 0.5f);
  st.pts.push_back({float(p.x()), float(p.y()), pr});
  st.build();
  m_liveChanged = true;
  refresh();
}

void InkCanvas::end() {
  if (m_g.kind == Gesture::Draw && !m_g.live.pts.empty()) {
    Stroke s = std::move(m_g.live);
    s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Page& pg = m_pages[m_g.page];
    int z = 0;
    for (const Stroke& o : pg.strokes) z = std::max(z, o.z + 1);
    for (const auto& v : pg.others) z = std::max(z, v.toObject()["z"].toInt() + 1);
    s.z = z;
    s.build();
    pg.strokes.push_back(s);
    record({m_g.page, {s}, {}});
    markDirty(m_g.page);
  } else if (m_g.kind == Gesture::Erase && !m_g.erased.empty()) {
    record({m_g.page, {}, m_g.erased});
  }
  m_g = Gesture();
  m_liveChanged = true;
  refresh();
}

void InkCanvas::eraseAt(int pi, QPointF p) {
  Page& pg = m_pages[pi];
  const float r = float(m_eraserWidth / 2);
  bool changed = false;
  for (size_t i = 0; i < pg.strokes.size();) {
    const Stroke& s = pg.strokes[i];
    bool hit = false;
    if (s.bounds.adjusted(-r, -r, r, r).contains(p)) {
      const float reach = r + s.width / 2;
      if (s.pts.size() == 1) hit = std::hypot(p.x() - s.pts[0].x, p.y() - s.pts[0].y) <= reach;
      for (size_t k = 1; k < s.pts.size() && !hit; ++k)
        hit = ink::segmentDistance(p, QPointF(s.pts[k - 1].x, s.pts[k - 1].y), QPointF(s.pts[k].x, s.pts[k].y)) <= reach;
    }
    if (hit) {
      m_g.erased.push_back(s);
      pg.strokes.erase(pg.strokes.begin() + long(i));
      changed = true;
    } else {
      ++i;
    }
  }
  if (changed) markDirty(pi);
}

// --- undo -------------------------------------------------------------------------------

void InkCanvas::record(Change c) {
  m_undo.push_back(std::move(c));
  if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
  m_redo.clear();
  emit historyChanged();
}

void InkCanvas::apply(const Change& c, bool reverse) {
  Page& pg = m_pages[c.page];
  const auto& remove = reverse ? c.added : c.removed;
  const auto& add = reverse ? c.removed : c.added;
  for (const Stroke& s : remove)
    pg.strokes.erase(std::remove_if(pg.strokes.begin(), pg.strokes.end(), [&](const Stroke& o) { return o.id == s.id; }), pg.strokes.end());
  for (const Stroke& s : add) pg.strokes.push_back(s);
  std::sort(pg.strokes.begin(), pg.strokes.end(), [](const Stroke& a, const Stroke& b) { return a.z < b.z; });
  markDirty(c.page);
}

void InkCanvas::undo() {
  if (m_undo.empty()) return;
  Change c = std::move(m_undo.back());
  m_undo.pop_back();
  apply(c, true);
  m_redo.push_back(std::move(c));
  emit historyChanged();
}

void InkCanvas::redo() {
  if (m_redo.empty()) return;
  Change c = std::move(m_redo.back());
  m_redo.pop_back();
  apply(c, false);
  m_undo.push_back(std::move(c));
  emit historyChanged();
}

// --- input ------------------------------------------------------------------------------

bool InkCanvas::eventFilter(QObject* watched, QEvent* event) {
  const auto type = event->type();
  if (watched != m_window || !(type == QEvent::TabletPress || type == QEvent::TabletMove || type == QEvent::TabletRelease)) return false;
  auto* te = static_cast<QTabletEvent*>(event);
  const QPointF local = mapFromScene(te->position());
  const bool inside = contains(local);
  m_lastPenMs = nowMs();
  // The eraser end and the side (barrel) button erase, when the settings say so.
  // (The side button as a lasso comes with the lasso tool.)
  const bool eraserEnd = m_eraserEndOn && te->pointerType() == QPointingDevice::PointerType::Eraser;
  const bool sideButton = m_sideButton == "eraser" && (te->buttons() & (Qt::RightButton | Qt::MiddleButton));
  if (type == QEvent::TabletPress) {
    if (!inside || !isVisible()) return false;
    emit penUsed();
    // The pen coming down while a resting palm "writes": drop the palm's stroke.
    if (m_g.touch && m_palmRejection) m_g = Gesture();
    begin(local, curved(te->pressure()), nowMs(), true, eraserEnd || sideButton);
    te->accept();
    return true;
  }
  if (type == QEvent::TabletMove) {
    if (m_g.kind == Gesture::None) {
      m_hover = local;
      m_hoverOnPage = inside;
      if (m_tool == "eraser") refresh();
      return false;
    }
    m_hover = local;
    extend(local, curved(te->pressure()), nowMs(), true);
    te->accept();
    return true;
  }
  if (m_g.kind == Gesture::None) return false;
  end();
  te->accept();
  return true;
}

void InkCanvas::mousePressEvent(QMouseEvent* e) {
  // Pens send tablet events (handled above); their copies as mouse events are ignored.
  if (e->device() && e->device()->type() == QInputDevice::DeviceType::Stylus) return e->ignore();
  forceActiveFocus();
  if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton || isPenOnlyBlocked(false)) {
    m_g = Gesture();
    m_g.kind = Gesture::Pan;
    m_g.lastScreen = e->position();
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  begin(e->position(), 0.5f, nowMs(), false, false);
}

void InkCanvas::mouseMoveEvent(QMouseEvent* e) {
  if (e->device() && e->device()->type() == QInputDevice::DeviceType::Stylus) return e->ignore();
  if (m_g.kind == Gesture::Pan) {
    m_scroll -= e->position() - m_g.lastScreen;
    m_g.lastScreen = e->position();
    clampScroll();
    emit viewChanged();
    refresh();
    return;
  }
  m_hover = e->position();
  extend(e->position(), 0.5f, nowMs(), false);
}

void InkCanvas::mouseReleaseEvent(QMouseEvent* e) {
  if (e->device() && e->device()->type() == QInputDevice::DeviceType::Stylus) return e->ignore();
  if (m_g.kind == Gesture::Pan) {
    m_g = Gesture();
    updateCursor();
    return;
  }
  end();
}

void InkCanvas::hoverMoveEvent(QHoverEvent* e) {
  m_hover = e->position();
  m_hoverOnPage = true;
  if (m_tool == "eraser") refresh();
}

void InkCanvas::wheelEvent(QWheelEvent* e) {
  if (e->modifiers() & Qt::ControlModifier) {
    zoomAt(m_zoom * std::exp(-e->angleDelta().y() * 0.0025), e->position());
    return;
  }
  const QPoint px = e->pixelDelta();
  // A wheel notch scrolls about as far as in a browser; touchpads give exact pixels.
  const QPointF d = !px.isNull() ? QPointF(px) : QPointF(e->angleDelta()) / 120.0 * 110.0;
  if (e->modifiers() & Qt::ShiftModifier) m_scroll.rx() -= d.y();
  else m_scroll -= d;
  clampScroll();
  emit viewChanged();
  refresh();
}

void InkCanvas::touchEvent(QTouchEvent* e) {
  const auto& pts = e->points();
  if (pts.size() >= 2) {
    // Two fingers: pinch to zoom and move.
    if (m_g.kind == Gesture::Draw) m_g = Gesture();  // a second finger cancels a touch stroke
    const QPointF a = pts[0].position(), b = pts[1].position();
    const QPointF mid = (a + b) / 2;
    const qreal dist = std::max<qreal>(1, QLineF(a, b).length());
    if (m_pinchStartDist == 0 || e->type() == QEvent::TouchBegin || pts[1].state() == QEventPoint::Pressed) {
      m_pinchStartDist = dist;
      m_pinchStartZoom = m_zoom;
      m_pinchMid = mid;
    } else {
      zoomAt(m_pinchStartZoom * dist / m_pinchStartDist, mid);
      m_scroll -= mid - m_pinchMid;
      m_pinchMid = mid;
      clampScroll();
      emit viewChanged();
      refresh();
    }
    e->accept();
    return;
  }
  m_pinchStartDist = 0;
  if (pts.isEmpty()) return;
  const QEventPoint& p = pts[0];
  const bool drawWithTouch = !m_penOnly;
  if (p.state() == QEventPoint::Pressed) {
    // A hand resting on the screen just after the pen: ignored.
    if (m_palmRejection && nowMs() - m_lastPenMs < 1500) {
      m_g = Gesture();
      e->accept();
      return;
    }
    if (drawWithTouch) {
      begin(p.position(), 0.5f, nowMs(), false, false);
      m_g.touch = true;
    }
    else {
      m_g = Gesture();
      m_g.kind = Gesture::Pan;
      m_g.lastScreen = p.position();
    }
  } else if (p.state() == QEventPoint::Updated) {
    if (m_g.kind == Gesture::None) {
      // Ignored (a palm), or the stroke was dropped.
    } else if (m_g.kind == Gesture::Pan) {
      m_scroll -= p.position() - m_g.lastScreen;
      m_g.lastScreen = p.position();
      clampScroll();
      emit viewChanged();
      refresh();
    } else {
      extend(p.position(), 0.5f, nowMs(), false);
    }
  } else if (p.state() == QEventPoint::Released) {
    if (m_g.kind == Gesture::Pan) m_g = Gesture();
    else end();
  }
  e->accept();
}

// --- loading what's in view -------------------------------------------------------

void InkCanvas::refresh() {
  polish();
  update();
}

void InkCanvas::updatePolish() {
  // GUI thread, just before the frame: load the pages that come into view.
  const QPointF o = originFor(width(), m_maxPageW * m_zoom, m_scroll);
  const qreal screen = height() / m_zoom;
  const qreal top = (-o.y()) / m_zoom - screen, bottom = (height() - o.y()) / m_zoom + screen;
  bool loaded = false;
  for (int i = 0; i < int(m_pages.size()); ++i) {
    const Page& p = m_pages[i];
    if (!p.loaded && p.top + p.h >= top && p.top <= bottom) {
      ensureLoaded(i);
      loaded = true;
    }
  }
  if (loaded) update();
}

// --- scene graph ------------------------------------------------------------------------

QSGNode* InkCanvas::updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) {
  // INKLEAF_PROFILE=1 prints how long each scene update takes.
  static const bool profile = qEnvironmentVariableIsSet("INKLEAF_PROFILE");
  if (!profile) return buildScene(old, data);
  QElapsedTimer t;
  t.start();
  QSGNode* n = buildScene(old, data);
  qInfo("scene update %.2f ms", t.nsecsElapsed() / 1e6);
  return n;
}

QSGNode* InkCanvas::buildScene(QSGNode* old, UpdatePaintNodeData*) {
  // root (no transform) → content transform → pages; live stroke; eraser ring.
  auto* root = old;
  QSGTransformNode* content;
  QSGTransformNode* liveT;
  QSGGeometryNode* live;
  QSGGeometryNode* ring;
  if (!root) {
    root = new QSGNode;
    content = new QSGTransformNode;
    liveT = new QSGTransformNode;
    live = meshNode({}, Qt::black);
    ring = meshNode({}, QColor(100, 100, 100, 200));
    liveT->appendChildNode(live);
    root->appendChildNode(content);
    root->appendChildNode(liveT);
    root->appendChildNode(ring);
    m_nodeState.clear();
    m_rebuildAll = true;
  } else {
    content = static_cast<QSGTransformNode*>(root->firstChild());
    liveT = static_cast<QSGTransformNode*>(content->nextSibling());
    live = static_cast<QSGGeometryNode*>(liveT->firstChild());
    ring = static_cast<QSGGeometryNode*>(liveT->nextSibling());
  }

  const QPointF o = originFor(width(), m_maxPageW * m_zoom, m_scroll);
  QMatrix4x4 m;
  m.translate(float(o.x()), float(o.y()));
  m.scale(float(m_zoom));
  content->setMatrix(m);
  content->markDirty(QSGNode::DirtyMatrix);

  // Pages near the view have nodes; they're dropped only once well out of
  // view (so scrolling back and forth doesn't rebuild them).
  const qreal screen = height() / m_zoom;
  const qreal viewTop = (-o.y()) / m_zoom, viewBottom = (height() - o.y()) / m_zoom;
  if (m_rebuildAll || m_nodeState.size() != m_pages.size()) {
    while (QSGNode* c = content->firstChild()) {
      content->removeChildNode(c);
      delete c;
    }
    m_nodeState.assign(m_pages.size(), {});
    for (size_t i = 0; i < m_pages.size(); ++i) {
      auto* t = new QSGTransformNode;
      content->appendChildNode(t);
      m_nodeState[i].node = t;
    }
    m_rebuildAll = false;
  }
  for (size_t i = 0; i < m_pages.size(); ++i) {
    Page& pg = m_pages[i];
    PageNodeState& st = m_nodeState[i];
    auto* pageT = static_cast<QSGTransformNode*>(st.node);
    const bool near = pg.top + pg.h >= viewTop - screen && pg.top <= viewBottom + screen;
    const bool far = pg.top + pg.h < viewTop - 4 * screen || pg.top > viewBottom + 4 * screen;
    if (far && st.lastPaper) {
      while (QSGNode* c = pageT->firstChild()) {
        pageT->removeChildNode(c);
        delete c;
      }
      st = PageNodeState{0, pageT, nullptr, {}};
      continue;
    }
    if (!near || !pg.loaded || st.version == pg.version) continue;
    if (!st.lastPaper) {
      // Paper once: shadow, sheet, lines.
      QMatrix4x4 pm;
      pm.translate((m_maxPageW - pg.w) / 2, pg.top);
      pageT->setMatrix(pm);
      pageT->appendChildNode(new QSGSimpleRectNode(QRectF(1, 2, pg.w, pg.h), QColor(0, 0, 0, 22)));
      pageT->appendChildNode(new QSGSimpleRectNode(QRectF(0, 0, pg.w, pg.h), pg.paper));
      for (const paper::Layer& l : paper::lines(pg.tmpl, pg.w, pg.h, pg.paper))
        if (!l.tris.empty()) pageT->appendChildNode(meshNode(l.tris, l.color));
      st.lastPaper = pageT->lastChild();
    }
    // Ink: only what changed. Erased strokes lose their node, new strokes get
    // one in z order; every other stroke's geometry stays on the GPU as it is.
    std::unordered_map<QString, bool> present;
    present.reserve(pg.strokes.size());
    for (const Stroke& s : pg.strokes) present[s.id] = true;
    for (auto it = st.strokes.begin(); it != st.strokes.end();) {
      if (!present.count(it->first)) {
        pageT->removeChildNode(it->second);
        delete it->second;
        it = st.strokes.erase(it);
      } else {
        ++it;
      }
    }
    QSGNode* prev = st.lastPaper;
    for (const Stroke& s : pg.strokes) {
      auto found = st.strokes.find(s.id);
      if (found != st.strokes.end()) {
        prev = found->second;
        continue;
      }
      if (!s.mesh || s.mesh->tris.empty()) continue;
      QSGNode* n = meshNode(s.mesh->tris, strokeColor(s.color, s.pen));
      pageT->insertChildNodeAfter(n, prev);
      st.strokes.emplace(s.id, n);
      prev = n;
    }
    st.version = pg.version;
  }

  // The stroke being written, in its page's place.
  if (m_liveChanged) {
    m_liveChanged = false;
    if (m_g.kind == Gesture::Draw && m_g.live.mesh) {
      const Page& pg = m_pages[m_g.page];
      QMatrix4x4 lm = m;
      lm.translate((m_maxPageW - pg.w) / 2, pg.top);
      liveT->setMatrix(lm);
      setMesh(live, m_g.live.mesh->tris, strokeColor(m_g.live.color, m_g.live.pen));
    } else {
      setMesh(live, {}, Qt::black);
    }
  } else if (m_g.kind == Gesture::Draw) {
    const Page& pg = m_pages[m_g.page];
    QMatrix4x4 lm = m;
    lm.translate((m_maxPageW - pg.w) / 2, pg.top);
    liveT->setMatrix(lm);
  }
  liveT->markDirty(QSGNode::DirtyMatrix);

  // Eraser ring under the pointer (screen pixels).
  std::vector<QPointF> ringTris;
  // Shown for the eraser tool, and while the pen's eraser end or side button erases.
  if ((m_tool == "eraser" || m_g.kind == Gesture::Erase) && m_hover.x() >= 0) {
    const qreal r = m_eraserWidth / 2 * m_zoom, t = 1.0;
    const int n = 48;
    for (int k = 0; k < n; ++k) {
      const double a0 = 2 * M_PI * k / n, a1 = 2 * M_PI * (k + 1) / n;
      const QPointF i0 = m_hover + QPointF(std::cos(a0), std::sin(a0)) * (r - t), o0 = m_hover + QPointF(std::cos(a0), std::sin(a0)) * r;
      const QPointF i1 = m_hover + QPointF(std::cos(a1), std::sin(a1)) * (r - t), o1 = m_hover + QPointF(std::cos(a1), std::sin(a1)) * r;
      ringTris.insert(ringTris.end(), {i0, o0, o1, i0, o1, i1});
    }
  }
  setMesh(ring, ringTris, QColor(90, 90, 90, 210));
  return root;
}
