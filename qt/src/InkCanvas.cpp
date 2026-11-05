#include "draw/Elements.h"
#include "InkCanvas.h"
#include <QJSValue>
#include <QTemporaryDir>
#include "PaperImage.h"
#include "editor/Geometry.h"

#include "daftar/DaftarEditor.h"

#include <QBuffer>
#include <QCursor>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QMatrix4x4>
#include <QPainter>
#include <QPdfWriter>
#include <QPixmap>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>
#include <QLineF>
#include <QTabletEvent>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cmath>

#include "Core.h"
#include "draw/Render.h"
#include "Canvas.h"
#include "Pdf.h"

/// Page pictures drawn the current way start with this (a data: URL parameter):
/// older ones are redrawn in the background.
static constexpr const char* kThumbPrefix = "data:image/jpeg;v=2;base64,";

namespace {
constexpr float kPageGap = 28;     // page units between pages
constexpr qreal kPad = 32;         // screen pixels beside the pages
constexpr qreal kPadTop = 74;      // above them (room for the tool drawer)
constexpr qreal kPadBottom = 84;   // below them (room for the page controls)
constexpr qreal kMinZoom = 0.25, kMaxZoom = 6;
constexpr float kLazyPen = 1.1f;   // screen pixels the ink may trail the pen
constexpr int kMaxTexture = 4096;  // pixels per side of one picture
constexpr double kLaserHold = 900, kLaserFade = 300, kLaserCoreFade = 480;

double nowMs() { return draw::clockMs(); }

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

QColor inkColor(const QColor& c, ink::Pen pen) { return ink::liveColor(c, pen); }

void clearChildren(QSGNode* n) {
  while (QSGNode* c = n->firstChild()) {
    n->removeChildNode(c);
    delete c;
  }
}

/// Picture scales in steps of √2, so small zoom changes reuse what's drawn.
double bucket(double s) { return std::clamp(std::pow(2.0, std::ceil(std::log2(std::max(s, 0.05)) * 2) / 2), 0.25, 16.0); }

}  // namespace

// --- setup ------------------------------------------------------------------------

InkCanvas::InkCanvas(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptTouchEvents(true);
  setAcceptHoverEvents(true);
  m_colors = {{"pen", "#111827"}, {"highlighter", "#facc15"}, {"shape", "#111827"}, {"text", "#111827"}, {"sticky", "#fef08a"}, {"laser", "#ff1f1f"}};
  m_widths = {{"pen", 3}, {"highlighter", 18}, {"eraser", 14}, {"shape", 3}, {"text", 20}, {"sticky", 18}};
  updateCursor();
  // A new desk colour (dark mode) repaints the strips around the pages.
  connect(this, &InkCanvas::settingsChanged, this, [this] { update(); });
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(600);
  connect(&m_saveTimer, &QTimer::timeout, this, &InkCanvas::flush);
  // Imported PDF pages get their thumbnails in the background, one at a time.
  m_scrollGlide.setInterval(8);
  m_scrollGlide.setTimerType(Qt::PreciseTimer);
  connect(&m_scrollGlide, &QTimer::timeout, this, [this] {
    const QPointF before = m_scroll;
    // Eases out over about a tenth of a second, however often this runs.
    const double now = nowMs();
    const double dt = std::clamp(now - m_glideLast, 1.0, 50.0);
    m_glideLast = now;
    m_scroll += (m_glideTarget - m_scroll) * (1.0 - std::exp(-dt / 32.0));
    if (QLineF(m_scroll, m_glideTarget).length() < 0.75) m_scroll = m_glideTarget;
    clampScroll();
    if (m_scroll == before || m_scroll == m_glideTarget) m_scrollGlide.stop();
    m_overlayDirty = true;
    emit viewChanged();
    refresh();
  });
  // While nothing happens, the pages around the view (several screens either
  // way) are loaded and drawn one at a time, so scrolling never waits for them.
  m_prefetch.setSingleShot(true);
  m_prefetch.setInterval(180);
  connect(&m_prefetch, &QTimer::timeout, this, [this] {
    if (prefetchStep()) m_prefetch.start(15);
  });
  m_thumbFill.setSingleShot(true);
  m_thumbFill.setInterval(250);
  // Pages without a picture yet (new pages, the daftar's, imported PDFs'):
  // one at a time while nothing else is going on.
  connect(&m_thumbFill, &QTimer::timeout, this, [this] {
    if (m_g.kind != Gesture::None || m_scrollGlide.isActive()) return m_thumbFill.start();
    for (int i = 0; i < int(m_pages.size()); ++i) {
      const QJsonObject& m = m_pages[i].meta;
      // Up to date (pictures from before the current way of drawing them are redone).
      const QString t = m["thumbnail"].toString();
      if (t.startsWith(QLatin1String(kThumbPrefix)) || (m_pages[i].tmpl.startsWith("cover") && !t.isEmpty())) continue;
      storeThumbnail(i);
      emit pagesChanged();
      if (m_pages[i].meta["thumbnail"].toString().startsWith(QLatin1String(kThumbPrefix))) m_thumbFill.start();
      return;
    }
  });
  m_settle.setSingleShot(true);
  m_settle.setInterval(160);
  connect(&m_settle, &QTimer::timeout, this, [this] {
    renderPictures();
    update();
  });
  // What a scratch-out took fades away (about a third of a second).
  m_fadeTick.setInterval(16);
  connect(&m_fadeTick, &QTimer::timeout, this, [this] {
    if (!m_ed) return m_fadeTick.stop();
    const double level = 1 - (nowMs() - m_fadeStart) / 320.0;
    if (level <= 0) {
      m_fadeTick.stop();
      m_ed->clearFade();
    } else {
      m_ed->setFade(level);
    }
    afterCore();
    update();
  });
  m_laserTick.setInterval(16);
  connect(&m_laserTick, &QTimer::timeout, this, [this] {
    const double now = nowMs();
    // The trail stays while the pen is down and a moment after, then fades.
    if (m_g.kind != Gesture::Laser && m_laserUp >= 0 && now - m_laserUp > kLaserHold + kLaserCoreFade) {
      m_laser.clear();
      m_laserUp = -1;
    }
    const bool fading = m_laserUp >= 0;
    if (m_laser.empty() || !fading) m_laserTick.stop();
    emit laserChanged();
  });
}

InkCanvas::~InkCanvas() { flush(); }

void InkCanvas::itemChange(ItemChange change, const ItemChangeData& value) {
  if (change == ItemSceneChange) {
    if (m_window) m_window->removeEventFilter(this);
    m_window = value.window;
    if (m_window) m_window->installEventFilter(this);
  }
  QQuickItem::itemChange(change, value);
}

// --- the editor core ----------------------------------------------------------------------

void InkCanvas::pushToolState() {
  if (!m_ed) return;
  ed::EditorCore& c = *m_ed;
  c.tool = m_tool;
  for (auto it = m_colors.begin(); it != m_colors.end(); ++it) c.colors[it.key()] = it.value().toString();
  for (auto it = m_widths.begin(); it != m_widths.end(); ++it) c.widths[it.key()] = it.value().toDouble();
  c.penStyle = m_penStyleName;
  c.penDash = m_penDash;
  c.penGap = m_penGap;
  c.streamline = m_smoothing;
  c.shapeKind = m_shapeKind;
  c.shapeFill = m_shapeFill;
  c.eraserMode = m_eraserMode;
  c.textFont = m_textFont;
  c.zoom = m_zoom;
  c.viewWidth = width();
}

void InkCanvas::afterCore() {
  if (!m_ed) return;
  ed::EditorCore& c = *m_ed;
  if (double(c.rev) != m_coreRev) {
    m_coreRev = double(c.rev);
    for (int i = 0; i < int(m_pages.size()); ++i)
      if (m_pages[i].synced) syncPage(i);
    emit historyChanged();
    if (c.dirty()) m_saveTimer.start();
  }
  if (c.scratches != m_scratches) {
    m_scratches = c.scratches;
    m_fadeStart = nowMs();
    m_fadeTick.start();
    emit scratchedOut();
  }
  if (double(c.overlayRev) != m_overlayRev) {
    m_overlayRev = double(c.overlayRev);
    m_overlayDirty = true;
  }
  if (double(c.instrumentRev) != m_instrumentRev) {
    m_instrumentRev = double(c.instrumentRev);
    m_instrument = c.instrument();
    emit instrumentChanged();
  }
  if (double(c.selectionRev) != m_selectionRev) {
    m_selectionRev = double(c.selectionRev);
    syncSelection();
  }
  // Selected items following the pointer.
  QHash<QString, QTransform> next;
  if (const auto lt = c.liveTransform()) {
    const QTransform t(lt->m.a, lt->m.b, lt->m.c, lt->m.d, lt->m.e, lt->m.f);
    for (const QString& id : lt->ids) next.insert(id, t);
  }
  if (next != m_liveTransform) {
    m_liveTransform = next;
    m_liveTransformDirty = true;
  }
  const bool hidden = c.liveHidden();
  if (hidden != m_liveHidden) {
    m_liveHidden = hidden;
    m_liveChanged = true;
  }
  refresh();
}

void InkCanvas::syncSelection() {
  m_selection = m_ed ? m_ed->selectionInfo() : QVariantMap();
  emit selectionChanged();
}

void InkCanvas::syncEditing() {
  QVariantMap next;
  if (m_ed && m_ed->editing()) {
    next = m_ed->editing()->toJson().toVariantMap();
    next["page"] = m_ed->active;
  }
  if (next != m_editing) {
    m_editing = next;
    emit editingChanged();
  }
}

/// Brings page `i`'s items in line with the core's elements (only changed ones are rebuilt).
void InkCanvas::syncPage(int i) {
  static const bool trace = qEnvironmentVariableIsSet("INKLEAF_TRACE");
  QElapsedTimer timer;
  timer.start();
  Page& pg = m_pages[i];
  // What changed since the page's items were built (an element's rev
  // changes when it does), in z order; hidden ones (being edited, or part
  // of a gesture) left out.
  struct Want {
    QString id;
    double rev;
  };
  std::vector<Want> want;
  std::unordered_map<QString, std::shared_ptr<Item>> fresh;
  const ed::Els& els = m_ed->load(i);
  want.reserve(els.size());
  int fetched = 0;
  for (const ed::ElPtr& el : els) {
    if (m_ed->isHidden(i, el->id)) continue;
    const double rev = double(el->rev);
    want.push_back({el->id, rev});
    auto found = pg.byId.find(el->id);
    if (found != pg.byId.end() && found->second->rev == rev) continue;
    ++fetched;
    auto it = std::make_shared<Item>();
    it->id = el->id;
    it->rev = rev;
    const ed::Rect& b = ed::EditorCore::boxOf(*el);
    const QRectF box(b.x, b.y, b.w, b.h);
    if (el->kind == ed::Kind::Stroke) {
      const QString style = el->style.value_or(QString());
      const bool hl = el->tool == "highlighter";
      if (ink::meshDrawn(style, hl)) {
        // Pen and highlighter ink: a mesh, as when it was written.
        std::vector<ink::Point> p;
        p.reserve(el->points.size());
        for (const ed::Pt& q : el->points) p.push_back({float(q[0]), float(q[1]), float(q[2])});
        it->mesh = true;
        const ink::Pen pen = ink::penFromName(style, hl);
        it->tris = std::make_shared<ink::Mesh>();
        ink::strokeMesh(p, pen, float(el->width), el->smooth ? float(*el->smooth) : ink::kSmoothNormal, *it->tris);
        it->color = inkColor(Canvas::parseColor(el->color.isEmpty() ? QStringLiteral("#111827") : el->color), pen);
      } else {
        // Textured nibs: drawn as pictures.
        it->data = el->toJson();
        it->rect = box.adjusted(-6, -6, 6, 6);
      }
    } else {
      it->data = el->toJson();
      const double pad = el->kind == ed::Kind::Sticky ? 18 : el->kind == ed::Kind::Shape ? 12 : 6;
      // Text: room for what fonts draw outside the line (Arabic dots and marks
      // above, tails below, swashes at the sides), which grows with the size.
      const double fs = el->kind == ed::Kind::Text ? el->fontSize.value_or(20) : 0;
      const double v = std::max(pad, fs * 0.75), h = std::max(pad, fs * 0.4);
      it->rect = box.adjusted(-h, -v, h, v);
    }
    fresh.emplace(it->id, it);
  }
  std::vector<std::shared_ptr<Item>> items;
  items.reserve(want.size());
  std::unordered_map<QString, std::shared_ptr<Item>> byId;
  bool changed = !pg.synced || want.size() != pg.items.size();
  for (const Want& w : want) {
    std::shared_ptr<Item> it;
    if (auto nf = fresh.find(w.id); nf != fresh.end()) {
      it = nf->second;
      changed = true;
      if (auto old = pg.byId.find(w.id); old != pg.byId.end()) m_graveyard.push_back(old->second);
    } else if (auto old = pg.byId.find(w.id); old != pg.byId.end()) {
      it = old->second;
    } else {
      continue;
    }
    if (!changed && (items.size() >= pg.items.size() || pg.items[items.size()] != it)) changed = true;
    items.push_back(it);
    byId.emplace(w.id, it);
  }
  for (auto& [id, it] : pg.byId)
    if (!byId.count(id) && !fresh.count(id)) {
      m_graveyard.push_back(it);
      changed = true;
    }
  pg.items = std::move(items);
  pg.byId = std::move(byId);
  pg.synced = true;
  if (changed) pg.version++;
  if (trace && timer.nsecsElapsed() > 3e6) qInfo("syncPage %d %.1f ms (%d fetched)", i, timer.nsecsElapsed() / 1e6, fetched);
}

// --- loading --------------------------------------------------------------------------

void InkCanvas::setNotebookId(const QString& id) {
  if (id == m_notebookId) return;
  flush();
  m_notebookId = id;
  emit notebookIdChanged();
  loadNotebook();
}

void InkCanvas::loadNotebook() {
  m_pages.clear();
  m_g = Gesture();
  m_scroll = {};
  m_fitPending = true;
  m_ed.reset();
  m_coreRev = m_overlayRev = m_selectionRev = -1;
  QJsonArray pages;
  if (!m_notebookId.isEmpty()) {
    QString err;
    pages = InkleafCore::instance()->callJson("pages", {{"notebookId", m_notebookId}}, &err).toArray();
    if (!err.isEmpty()) emit message(err, true);
    m_ed = std::make_unique<ed::EditorCore>(pages);
    // Holding a stroke still turns it into a shape when the timer fires.
    m_ed->onTimer = [this] { afterCore(); };
    pushToolState();
  }
  applyPages(pages);
  if (width() > 0) fitWidth();
  m_rebuildAll = true;
  afterCore();
  syncSelection();
  syncEditing();
  emit viewChanged();
}

void InkCanvas::applyPages(const QJsonArray& pages) {
  std::vector<Page> next;
  for (const auto& v : pages) {
    const QJsonObject o = v.toObject();
    Page p;
    p.id = o["id"].toString();
    // Keep what's built for pages already shown.
    auto was = std::find_if(m_pages.begin(), m_pages.end(), [&](const Page& q) { return q.id == p.id; });
    if (was != m_pages.end()) p = std::move(*was);
    const bool paperChanged = p.tmpl != o["template"].toString("blank") || p.paper != o["paperColor"].toString("#fffdf7") || p.w != float(o["width"].toDouble(794));
    p.meta = o;
    p.w = float(o["width"].toDouble(794));
    p.h = float(o["height"].toDouble(1123));
    p.tmpl = o["template"].toString("blank");
    p.paper = o["paperColor"].toString("#fffdf7");
    if (paperChanged) p.paperScale = p.detailScale = 0;
    next.push_back(std::move(p));
  }
  for (Page& old : m_pages)
    for (auto& it : old.items) m_graveyard.push_back(it);
  m_pages = std::move(next);
  layoutPages();
  m_rebuildAll = true;
  emit pagesChanged();
  refresh();
  m_thumbFill.start(1500);
}

QVariantList InkCanvas::pages() const {
  QVariantList out;
  for (const Page& p : m_pages) out << p.meta.toVariantMap();
  return out;
}

void InkCanvas::reloadPages(bool elementsToo) {
  if (m_notebookId.isEmpty()) return;
  flush();
  if (elementsToo) {
    if (m_ed) m_ed->forgetAll();
    for (Page& p : m_pages) {
      for (auto& it : p.items) m_graveyard.push_back(it);
      p.items.clear();
      p.byId.clear();
      p.synced = false;
    }
  }
  QString err;
  const QJsonArray pages = InkleafCore::instance()->callJson("pages", {{"notebookId", m_notebookId}}, &err).toArray();
  if (!err.isEmpty()) return emit message(err, true);
  if (m_ed) m_ed->setPages(pages);
  applyPages(pages);
  for (int i = 0; i < int(m_pages.size()); ++i) m_pages[i].synced = false;
  m_coreRev = -1;
  afterCore();
}

QVariant InkCanvas::daftarQuery(const QString& fn, const QVariantList& args) {
  if (!m_ed) return {};
  return daftar::runInEditor(*m_ed, fn, args);
}

QVariantMap InkCanvas::daftar(const QString& fn, const QVariantList& args) {
  if (!m_ed) return {};
  flush();
  QVariantMap r = daftarQuery(fn, args).toMap();
  if (r.value("reloadAll").toBool() || r.value("reload").toBool()) {
    reloadPages(r.value("reloadAll").toBool());
    const QString id = r.value("pageId").toString();
    for (int i = 0; i < int(m_pages.size()); ++i)
      if (m_pages[i].meta.value("id").toString() == id) r.insert("goTo", i);
  }
  if (r.value("again").toBool()) return daftar(fn, args);
  afterCore();
  if (r.contains("goTo") && !r.value("goTo").isNull()) {
    const int i = r.value("goTo").toInt();
    if (r.contains("y") && i >= 0 && i < int(m_pages.size())) {
      // The line written, a third of the way down the view.
      m_scroll.setY((m_pages[i].top + r.value("y").toDouble()) * m_zoom - height() * 0.35);
      clampScroll();
      emit viewChanged();
      refresh();
    } else {
      goToPage(i);
    }
  }
  if (r.contains("toast")) emit message(r.value("toast").toString(), false);
  if (r.contains("error")) emit message(r.value("error").toString(), true);
  return r;
}

void InkCanvas::flush() {
  m_saveTimer.stop();
  if (!m_ed) return;
  const std::vector<int> dirty = m_ed->takeDirty();
  if (dirty.empty()) return;
  for (const int i : dirty) {
    if (i < 0 || i >= int(m_pages.size())) continue;
    // The stored form, written here: [{ id, kind, z, data: element }] with
    // each stroke's points written short.
    const ed::Els& els = m_ed->load(i);
    QByteArray out;
    out.reserve(4096);
    out += "{\"pageId\":";
    out += QJsonDocument(QJsonArray{m_pages[i].id}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
    out += ",\"elements\":[";
    bool first = true;
    auto num = [&](double v, int decimals) {
      // Short decimals, like the web app's stored points.
      QByteArray t = QByteArray::number(double(float(v)), 'f', decimals);
      if (t.contains('.')) {
        while (t.endsWith('0')) t.chop(1);
        if (t.endsWith('.')) t.chop(1);
      }
      if (t == "-0") t = "0";
      out += t;
    };
    for (const ed::ElPtr& e : els) {
      const bool stroke = e->kind == ed::Kind::Stroke;
      const QJsonObject el = e->toJson(false);
      if (!first) out += ',';
      first = false;
      out += "{\"id\":" + QJsonDocument(QJsonArray{el["id"]}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
      out += ",\"kind\":" + QJsonDocument(QJsonArray{el["kind"]}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
      out += ",\"z\":" + QByteArray::number(el["z"].toDouble());
      out += ",\"data\":";
      QByteArray data = QJsonDocument(el).toJson(QJsonDocument::Compact);
      if (stroke) {
        data.chop(1);  // the closing brace: the points go in before it
        out += data;
        out += data.endsWith('{') ? "\"points\":[" : ",\"points\":[";
        for (size_t q = 0; q < e->points.size(); ++q) {
          if (q) out += ',';
          out += '[';
          num(e->points[q][0], 2);
          out += ',';
          num(e->points[q][1], 2);
          out += ',';
          num(e->points[q][2], 3);
          out += ']';
        }
        out += "]}";
      } else {
        out += data;
      }
      out += '}';
    }
    out += "]}";
    QString err;
    InkleafCore::instance()->callRaw("saveElements", out, &err);
    if (!err.isEmpty()) {
      m_ed->markDirty(i);
      emit message(err, true);
      continue;
    }
    // A new page picture for the library and the sidebar.
    storeThumbnail(i);
  }
  emit pagesChanged();
}

void InkCanvas::storeThumbnail(int i) {
  if (i < 0 || i >= int(m_pages.size())) return;
  Page& p = m_pages[i];
  if (!p.synced) syncPage(i);
  // Covers are drawn sharper: the library shows them full-size. Drawn at the
  // picture's own size (not the screen's paper shrunk, which skipped rows: ruled
  // lines came out uneven and thin lines like the margin vanished).
  const bool cover = p.tmpl.startsWith("cover");
  const double s = (cover ? 420.0 : 300.0) / p.w;
  QImage bg;
  if (!p.meta["backgroundAsset"].toString().isEmpty() && !p.meta["backgroundPage"].isNull()) bg = paperWithBackground(p, s, {});
  const auto c = draw::renderPage(p.tmpl, p.paper, p.w, p.h, sceneJson(i), s, bg);
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  c->image().save(&buf, "JPEG", 85);
  const QString url = QString::fromLatin1(kThumbPrefix) + QString::fromLatin1(bytes.toBase64());
  QString err;
  InkleafCore::instance()->callJson("setPageThumbnail", {{"pageId", p.id}, {"thumbnail", url}}, &err);
  if (!err.isEmpty()) return emit message(err, true);
  p.meta["thumbnail"] = url;
}

// --- page operations -------------------------------------------------------------------

void InkCanvas::addPage(int index, const QString& tmpl) {
  flush();
  QString err;
  QJsonObject args{{"notebookId", m_notebookId}, {"index", index < 0 ? QJsonValue() : QJsonValue(index)}, {"template", tmpl.isEmpty() ? QJsonValue() : QJsonValue(tmpl)}};
  InkleafCore::instance()->callJson("addPage", args, &err);
  if (!err.isEmpty()) return emit message(err, true);
  reloadPages();
  goToPage(index < 0 ? int(m_pages.size()) - 1 : index);
}

void InkCanvas::duplicatePage(int index) {
  if (index < 0 || index >= int(m_pages.size())) return;
  flush();
  QString err;
  InkleafCore::instance()->callJson("duplicatePage", {{"pageId", m_pages[index].id}}, &err);
  if (!err.isEmpty()) return emit message(err, true);
  reloadPages();
  goToPage(index + 1);
}

void InkCanvas::deletePage(int index) {
  if (index < 0 || index >= int(m_pages.size()) || m_pages.size() <= 1) return;
  flush();
  QString err;
  InkleafCore::instance()->callJson("deletePage", {{"pageId", m_pages[index].id}}, &err);
  if (!err.isEmpty()) return emit message(err, true);
  reloadPages();
}

void InkCanvas::movePage(int index, int to) {
  if (index < 0 || index >= int(m_pages.size()) || to < 0 || to >= int(m_pages.size()) || index == to) return;
  flush();
  QString err;
  InkleafCore::instance()->callJson("movePage", {{"pageId", m_pages[index].id}, {"newIndex", to}}, &err);
  if (!err.isEmpty()) return emit message(err, true);
  reloadPages();
}

void InkCanvas::togglePageFavourite(int index) {
  if (index < 0 || index >= int(m_pages.size())) return;
  QString err;
  InkleafCore::instance()->callJson("setPageFavourite", {{"pageId", m_pages[index].id}, {"favourite", !m_pages[index].meta["favourite"].toBool()}}, &err);
  if (!err.isEmpty()) return emit message(err, true);
  reloadPages();
}

void InkCanvas::setPaper(int index, const QString& tmpl, const QString& color, bool allPages) {
  flush();
  for (int i = 0; i < int(m_pages.size()); ++i) {
    if (!allPages && i != index) continue;
    const Page& p = m_pages[i];
    QString err;
    InkleafCore::instance()->callJson("setPageTemplate", {{"pageId", p.id}, {"template", tmpl.isEmpty() ? p.tmpl : tmpl}, {"paperColor", color.isEmpty() ? QJsonValue(p.paper) : QJsonValue(color)}}, &err);
    if (!err.isEmpty()) return emit message(err, true);
  }
  reloadPages();
  // The library picture follows the new paper.
  for (int i = 0; i < int(m_pages.size()); ++i)
    if (allPages || i == index) {
      m_ed->load(i);
      m_ed->markDirty(i);
    }
  flush();
}

// --- the notebook's settings: contents -------------------------------------------------

QVariant InkCanvas::notebookSetting(const QString& key) {
  if (m_notebookId.isEmpty()) return {};
  const QJsonObject nb = InkleafCore::instance()->callJson("notebook", {{"id", m_notebookId}}).toObject();
  const QJsonObject meta = QJsonDocument::fromJson(nb["meta"].toString().toUtf8()).object();
  return meta.value(key).toVariant();
}

void InkCanvas::setNotebookSetting(const QString& key, const QVariant& value) {
  if (m_notebookId.isEmpty()) return;
  const QJsonObject nb = InkleafCore::instance()->callJson("notebook", {{"id", m_notebookId}}).toObject();
  QJsonObject meta = QJsonDocument::fromJson(nb["meta"].toString().toUtf8()).object();
  // (A list or object from QML comes wrapped as a script value.)
  const QVariant plain = value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
  const QJsonValue v = QJsonValue::fromVariant(plain);
  if (v.isNull() || v.isUndefined() || (v.isArray() && v.toArray().isEmpty())) meta.remove(key);
  else meta[key] = v;
  QString err;
  InkleafCore::instance()->callJson("setNotebookMeta", {{"id", m_notebookId}, {"meta", meta.isEmpty() ? QJsonValue() : QJsonValue(QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)))}}, &err);
  if (!err.isEmpty()) return emit message(err, true);
  emit notebookSettingsChanged();
}

QVariantList InkCanvas::contents() {
  struct E {
    int page, order;
    QVariantMap v;
  };
  std::vector<E> all;
  int order = 0;
  // The PDFs' own contents, on the pages that show them.
  QHash<QString, QHash<int, int>> shown;  // asset → its page → notebook page
  for (int i = 0; i < int(m_pages.size()); ++i) {
    const QString asset = m_pages[i].meta["backgroundAsset"].toString();
    if (!asset.isEmpty() && !m_pages[i].meta["backgroundPage"].isNull()) {
      auto& pages = shown[asset];
      const int bp = m_pages[i].meta["backgroundPage"].toInt();
      if (!pages.contains(bp)) pages.insert(bp, i);
    }
  }
  for (auto it = shown.constBegin(); it != shown.constEnd(); ++it)
    for (const PdfFile::Entry& e : PdfDocs::instance().outline(it.key()))
      if (it->contains(e.page))
        all.push_back({it->value(e.page), order++, {{"kind", "pdf"}, {"title", e.title}, {"level", std::min(e.level, 3)}, {"page", it->value(e.page)}}});
  // A daftar's classes: where each one's pages start.
  QString section;
  for (int i = 0; i < int(m_pages.size()); ++i) {
    const QString sec = m_pages[i].meta["section"].toString();
    if (!sec.isEmpty() && sec != section) all.push_back({i, order++, {{"kind", "section"}, {"title", sec}, {"level", 0}, {"page", i}}});
    section = sec;
  }
  // Your headings.
  QHash<QString, int> index;
  for (int i = 0; i < int(m_pages.size()); ++i) index.insert(m_pages[i].id, i);
  for (const QVariant& h : notebookSetting("contents").toList()) {
    const QVariantMap m = h.toMap();
    const auto at = index.constFind(m.value("page").toString());
    if (at == index.constEnd()) continue;  // its page was deleted
    all.push_back({*at, order++, {{"kind", "heading"}, {"id", m.value("id")}, {"title", m.value("title")}, {"level", std::clamp(m.value("level").toInt(), 0, 3)}, {"page", *at}}});
  }
  std::stable_sort(all.begin(), all.end(), [](const E& a, const E& b) { return a.page != b.page ? a.page < b.page : a.order < b.order; });
  QVariantList out;
  for (const E& e : all) out.append(e.v);
  return out;
}

void InkCanvas::addHeading(int index, const QString& title, int level) {
  if (index < 0 || index >= int(m_pages.size()) || title.trimmed().isEmpty()) return;
  QVariantList list = notebookSetting("contents").toList();
  list.append(QVariantMap{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"page", m_pages[index].id}, {"title", title.trimmed().left(200)}, {"level", std::clamp(level, 0, 3)}});
  setNotebookSetting("contents", list);
}

void InkCanvas::changeHeading(const QString& id, const QVariantMap& change) {
  QVariantList list = notebookSetting("contents").toList();
  for (QVariant& h : list) {
    QVariantMap m = h.toMap();
    if (m.value("id").toString() != id) continue;
    if (change.contains("title") && !change.value("title").toString().trimmed().isEmpty()) m["title"] = change.value("title").toString().trimmed().left(200);
    if (change.contains("level")) m["level"] = std::clamp(change.value("level").toInt(), 0, 3);
    h = m;
  }
  setNotebookSetting("contents", list);
}

void InkCanvas::removeHeading(const QString& id) {
  QVariantList list = notebookSetting("contents").toList();
  list.erase(std::remove_if(list.begin(), list.end(), [&](const QVariant& h) { return h.toMap().value("id").toString() == id; }), list.end());
  setNotebookSetting("contents", list);
}

QString InkCanvas::savePageAsTemplate(int index, const QString& name) {
  if (index < 0 || index >= int(m_pages.size())) return "No page to keep";
  // The page as a one-page PDF (vectors: it stays sharp at any size).
  QTemporaryDir dir;
  if (!dir.isValid()) return "Couldn't keep the page";
  const QString file = dir.filePath("template.pdf");
  const QString err = exportPdf(file, {index}, name);
  if (!err.isEmpty()) return err;
  QFile f(file);
  if (!f.open(QIODevice::ReadOnly)) return "Couldn't keep the page";
  const QByteArray bytes = f.readAll();
  const Page& p = m_pages[index];
  const QString preview = exportPage(index, std::min(1.0, 360.0 / std::max(p.w, p.h)), false).value("dataUrl").toString();
  QString e;
  InkleafCore::instance()->callJson("savePageTemplate", {{"name", name}, {"preview", preview}, {"width", p.w}, {"height", p.h}, {"notebookId", m_notebookId}}, &e, bytes);
  return e;
}

void InkCanvas::usePageTemplate(int index, const QString& templateId) {
  if (index < 0 || index >= int(m_pages.size())) return;
  finishEdit();
  flush();
  QString err;
  InkleafCore::instance()->callJson("usePageTemplate", {{"pageId", m_pages[index].id}, {"templateId", templateId}}, &err);
  if (!err.isEmpty()) return emit message(err, true);
  reloadPages();
  // The library picture follows the new paper.
  m_ed->load(index);
  m_ed->markDirty(index);
  flush();
}

void InkCanvas::addTemplatePage(int index, const QString& templateId) {
  addPage(index);
  const int at = index < 0 ? int(m_pages.size()) - 1 : std::min(index, int(m_pages.size()) - 1);
  usePageTemplate(at, templateId);
  goToPage(at);
}

QJsonArray InkCanvas::sceneJson(int index) {
  QJsonArray out;
  if (!m_ed) return out;
  for (const ed::ElPtr& e : m_ed->load(index)) out.append(e->toJson());
  return out;
}

QString InkCanvas::pagePreview(int index, qreal scale) {
  if (index < 0 || index >= int(m_pages.size())) return {};
  const Page& p = m_pages[index];
  return draw::renderPage(p.tmpl, p.paper, p.w, p.h, sceneJson(index), scale)->toDataURL("image/png");
}

QString InkCanvas::exportPdf(const QString& path, const QVariantList& indexes, const QString& title) {
  const QString file = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
  if (indexes.isEmpty()) return "Nothing to export";
  finishEdit();
  flush();
  // Pages are drawn by the same code as the screen, as vectors: lines and
  // ink stay sharp, text stays text (Qt embeds and shapes the fonts).
  QPdfWriter writer(file);
  writer.setResolution(96);  // 1 device unit = 1 page unit (CSS px)
  writer.setCreator("Inkleaf");
  writer.setTitle(title);
  const auto sizeOf = [&](int i) {
    const Page& p = m_pages[i];
    return QPageSize(QSizeF(p.w * 0.75, p.h * 0.75), QPageSize::Point, QString(), QPageSize::ExactMatch);
  };
  int first = -1;
  for (const QVariant& v : indexes)
    if (v.toInt() >= 0 && v.toInt() < int(m_pages.size())) {
      first = v.toInt();
      break;
    }
  if (first < 0) return "Nothing to export";
  writer.setPageLayout(QPageLayout(sizeOf(first), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
  QPainter painter;
  if (!painter.begin(&writer)) return "Couldn't write " + file;
  bool started = false;
  for (const QVariant& v : indexes) {
    const int i = v.toInt();
    if (i < 0 || i >= int(m_pages.size())) continue;
    const Page& p = m_pages[i];
    if (started) {
      writer.setPageLayout(QPageLayout(sizeOf(i), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
      writer.newPage();
    }
    started = true;
    QImage bg;
    const QString asset = p.meta["backgroundAsset"].toString();
    if (!asset.isEmpty() && !p.meta["backgroundPage"].isNull()) {
      const int bp = p.meta["backgroundPage"].toInt();
      const QImage img = PdfDocs::instance().render(asset, bp, 3 * p.w / std::max(1.0, PdfDocs::instance().pageSize(asset, bp).width()));
      if (!img.isNull()) bg = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    {
      Canvas canvas(&painter, int(std::ceil(p.w)), int(std::ceil(p.h)));
      draw::paintPage(canvas, p.tmpl, p.paper, p.w, p.h, sceneJson(i), bg);
    }  // the canvas restores the painter for the next page
  }
  painter.end();
  return {};
}

QVariantMap InkCanvas::exportPage(int index, qreal scale, bool jpeg) {
  if (index < 0 || index >= int(m_pages.size())) return {};
  const Page& p = m_pages[index];
  // An imported PDF page as the background.
  QImage bg;
  const QString asset = p.meta["backgroundAsset"].toString();
  if (!asset.isEmpty() && !p.meta["backgroundPage"].isNull()) {
    const QImage img = PdfDocs::instance().render(asset, p.meta["backgroundPage"].toInt(), scale * p.w / std::max(1.0, PdfDocs::instance().pageSize(asset, p.meta["backgroundPage"].toInt()).width()));
    if (!img.isNull()) bg = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  }
  const auto c = draw::renderPage(p.tmpl, p.paper, p.w, p.h, sceneJson(index), scale, bg);
  return {{"dataUrl", c->toDataURL(jpeg ? "image/jpeg" : "image/png", 0.9)}, {"widthPx", c->width()}, {"heightPx", c->height()}, {"pageWidth", p.w}, {"pageHeight", p.h}};
}

// --- tool state -------------------------------------------------------------------------

void InkCanvas::setTool(const QString& t) {
  if (t == m_tool) return;
  m_tool = t;
  if (m_ed) {
    m_ed->finishTextEdit();
    if (t != "lasso") m_ed->clearSelection();
  }
  pushToolState();
  updateCursor();
  emit toolChanged();
  syncEditing();
  afterCore();
}

void InkCanvas::setPenStyle(const QString& s) {
  if (s == m_penStyleName) return;
  m_penStyleName = s;
  pushToolState();
  emit toolStateChanged();
}
void InkCanvas::setPenDash(const QString& s) {
  if (s == m_penDash) return;
  m_penDash = s;
  pushToolState();
  emit toolStateChanged();
}
void InkCanvas::setPenGap(double g) {
  g = std::clamp(g, 0.5, 4.0);
  if (g == m_penGap) return;
  m_penGap = g;
  pushToolState();
  emit toolStateChanged();
}
void InkCanvas::setColors(const QVariantMap& c) {
  if (c == m_colors) return;
  m_colors = c;
  emit laserChanged();
  pushToolState();
  emit toolStateChanged();
}
void InkCanvas::setWidths(const QVariantMap& w) {
  if (w == m_widths) return;
  m_widths = w;
  pushToolState();
  emit toolStateChanged();
  refresh();
}
void InkCanvas::setShapeKind(const QString& k) {
  if (k == m_shapeKind) return;
  m_shapeKind = k;
  pushToolState();
  emit toolStateChanged();
}
void InkCanvas::setShapeFill(bool f) {
  if (f == m_shapeFill) return;
  m_shapeFill = f;
  pushToolState();
  emit toolStateChanged();
}
void InkCanvas::setEraserMode(const QString& m) {
  if (m == m_eraserMode) return;
  m_eraserMode = m;
  pushToolState();
  emit toolStateChanged();
}
void InkCanvas::setTextFont(const QString& f) {
  if (f == m_textFont) return;
  m_textFont = f;
  pushToolState();
  emit toolStateChanged();
}
void InkCanvas::setSmoothing(qreal s) {
  if (qFuzzyCompare(s + 1, m_smoothing + 1)) return;
  m_smoothing = s;
  pushToolState();
  emit toolStateChanged();
}
void InkCanvas::setPenOnly(bool on) {
  if (on == m_penOnly) return;
  m_penOnly = on;
  emit penOnlyChanged();
}
void InkCanvas::setPenCursor(const QString& c) {
  if (c == m_penCursor) return;
  m_penCursor = c;
  updateCursor();
  emit settingsChanged();
}
void InkCanvas::showInstrument(const QString& kind) {
  if (!m_ed) return;
  const bool shown = m_instrument.value("visible").toBool();
  if (kind.isEmpty() || (shown && m_instrument.value("kind").toString() == kind)) {
    m_ed->hideInstrument();
  } else {
    // In the middle of the view, on the page in view.
    const int pi = currentPage();
    m_ed->activate(pi);
    QPointF c = toPageOf(pi, QPointF(width() / 2, height() / 2));
    if (pi < int(m_pages.size())) c = QPointF(std::clamp<qreal>(c.x(), 0, m_pages[pi].w), std::clamp<qreal>(c.y(), 0, m_pages[pi].h));
    if (shown) m_ed->hideInstrument();
    m_ed->showInstrument(kind, c.x(), c.y());
  }
  afterCore();
}

void InkCanvas::placeInstrument(qreal cx, qreal cy, qreal angle) {
  if (m_ed) m_ed->placeInstrument(cx, cy, angle);
  afterCore();
}

void InkCanvas::setInstrumentArm(int arm, qreal degrees) {
  if (m_ed) m_ed->setArm(arm, degrees);
  afterCore();
}

bool InkCanvas::instrumentContains(qreal x, qreal y) { return m_ed && m_ed->instrumentContains(x, y); }

QPointF InkCanvas::itemToPage(QPointF p) const { return toPageOf(activePage(), p); }

float InkCanvas::curved(qreal pressure) const {
  const qreal gamma = m_pressureCurve == "light" ? 0.6 : m_pressureCurve == "firm" ? 1.6 : 1.0;
  return float(std::pow(std::clamp<qreal>(pressure, 0, 1), gamma));
}

// --- the laser (drawn by LaserTrail) ---------------------------------------------------

qreal InkCanvas::laserColorAlpha() const {
  if (m_laserUp < 0) return 1;
  const double t = nowMs() - m_laserUp - kLaserHold;
  return t <= 0 ? 1 : std::max(0.0, 1 - t / kLaserFade);
}

qreal InkCanvas::laserCoreAlpha() const {
  if (m_laserUp < 0) return 1;
  const double t = nowMs() - m_laserUp - kLaserHold;
  return t <= 0 ? 1 : std::max(0.0, 1 - t / kLaserCoreFade);
}

QColor InkCanvas::laserColor() const { return Canvas::parseColor(m_colors.value("laser", "#ff1f1f").toString()); }

void InkCanvas::updateCursor() {
  if (m_tool == "eraser") return setCursor(Qt::BlankCursor);
  if (m_tool == "hand") return setCursor(Qt::OpenHandCursor);
  if (m_tool == "lasso") return setCursor(Qt::ArrowCursor);
  if (m_tool == "text" || m_tool == "sticky") return setCursor(Qt::IBeamCursor);
  if (m_tool == "laser") return setCursor(Qt::BlankCursor);  // the dot is drawn (LaserTrail)
  if (m_penCursor == "none") return setCursor(Qt::BlankCursor);
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

// --- editing commands --------------------------------------------------------------------

bool InkCanvas::canUndo() const { return m_ed && m_ed->canUndo(); }
bool InkCanvas::canRedo() const { return m_ed && m_ed->canRedo(); }

void InkCanvas::undo() {
  if (m_ed) m_ed->clearFade(), m_ed->undo();
  syncEditing();
  afterCore();
}
void InkCanvas::redo() {
  if (m_ed) m_ed->redo();
  syncEditing();
  afterCore();
}
void InkCanvas::deleteSelection() {
  if (m_ed) m_ed->deleteSelection();
  afterCore();
}
void InkCanvas::duplicateSelection() {
  if (m_ed) m_ed->duplicateSelection();
  afterCore();
}
int InkCanvas::copySelection() { return m_ed ? m_ed->copySelection() : 0; }
int InkCanvas::cutSelection() {
  const int n = m_ed ? m_ed->cutSelection() : 0;
  afterCore();
  return n;
}
int InkCanvas::paste() {
  const int n = m_ed ? m_ed->pasteClipboard() : 0;
  if (n) setTool("lasso");
  afterCore();
  return n;
}
void InkCanvas::selectAll() {
  setTool("lasso");
  if (m_ed) m_ed->selectAll();
  afterCore();
}
void InkCanvas::clearSelection() {
  if (m_ed) m_ed->clearSelection();
  afterCore();
}
void InkCanvas::recolorSelection(const QString& color) {
  if (m_ed) m_ed->recolorSelection(color);
  afterCore();
}
void InkCanvas::styleSelection(const QVariantMap& change) {
  if (m_ed) m_ed->styleSelectedText(change);
  afterCore();
}
void InkCanvas::changeShape(const QString& change, qreal value) {
  if (m_ed) m_ed->updateSelectedShape(change, value);
  afterCore();
}

QVariantList InkCanvas::selectedInkStrokes() { return m_ed ? m_ed->selectedInkStrokes() : QVariantList(); }

QString InkCanvas::selectionImage(int maxSide) {
  if (!m_ed) return {};
  const auto box = m_ed->selectionBounds();
  const int index = activePage();
  if (!box || index < 0 || index >= int(m_pages.size())) return {};
  const Page& p = m_pages[index];
  const double pad = 16;
  const QRectF region = QRectF(box->x - pad, box->y - pad, box->w + 2 * pad, box->h + 2 * pad).intersected(QRectF(0, 0, p.w, p.h));
  if (region.isEmpty()) return {};
  const double scale = std::clamp(double(maxSide) / std::max(region.width(), region.height()), 0.5, 4.0);
  // The whole page at that scale (its paper and PDF too), then the selection's part.
  QImage bg;
  const QString asset = p.meta["backgroundAsset"].toString();
  if (!asset.isEmpty() && !p.meta["backgroundPage"].isNull()) {
    const QImage img = PdfDocs::instance().render(asset, p.meta["backgroundPage"].toInt(), scale * p.w / std::max(1.0, PdfDocs::instance().pageSize(asset, p.meta["backgroundPage"].toInt()).width()));
    if (!img.isNull()) bg = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  }
  const auto c = draw::renderPage(p.tmpl, p.paper, p.w, p.h, sceneJson(index), scale, bg);
  const QImage part = c->image().copy(QRect(int(region.x() * scale), int(region.y() * scale), int(region.width() * scale), int(region.height() * scale)));
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  part.save(&buf, "PNG");
  return "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
}

void InkCanvas::insertGraph(const QVariantMap& spec) {
  if (!m_ed) return;
  finishEdit();
  const auto box = m_ed->selectionBounds();
  const int pi = box ? activePage() : currentPage();
  if (pi < 0 || pi >= int(m_pages.size())) return;
  m_ed->activate(pi);
  const double pw = m_pages[pi].w;
  const double w = std::min(360.0, pw * 0.6), h = w * 0.8;
  double x, y;
  if (box && box->x + box->w + 16 + w <= pw) {
    x = box->x + box->w + 16;
    y = box->y;
  } else if (box) {
    x = box->x;
    y = box->y + box->h + 16;
  } else {
    const QPointF c = toPageOf(pi, QPointF(width() / 2, height() / 2));
    x = c.x() - w / 2;
    y = c.y() - h / 2;
  }
  setTool("lasso");
  m_ed->insertGraph(spec, x, y, w, h);
  afterCore();
}

bool InkCanvas::updateGraph(const QVariantMap& spec) {
  if (!m_ed || !m_ed->updateSelectedGraph(spec)) return false;
  afterCore();
  return true;
}

// --- the zoom box -----------------------------------------------------------------------

void InkCanvas::zoomBoxPress(int page, qreal x, qreal y, qreal pressure, bool stylus, qreal mag) {
  if (page < 0 || page >= int(m_pages.size())) return;
  m_inputMag = std::max(1.0, double(mag));
  press(pageRectToItem(page, QRectF(x, y, 0, 0)).topLeft(), float(pressure), stylus, QString(), false, {});
  emit liveInkChanged();
}

void InkCanvas::zoomBoxDrag(qreal x, qreal y, qreal pressure, bool stylus) {
  if (m_g.page < 0 || m_g.page >= int(m_pages.size())) return;
  drag(pageRectToItem(m_g.page, QRectF(x, y, 0, 0)).topLeft(), float(pressure), stylus, {});
  emit liveInkChanged();
}

void InkCanvas::zoomBoxRelease() {
  release();
  m_inputMag = 1;
  emit liveInkChanged();
}

void InkCanvas::ensureVisible(int page, const QRectF& rect) {
  if (page < 0 || page >= int(m_pages.size())) return;
  const QRectF r = pageRectToItem(page, rect);
  const QRectF view(0, 0, width(), height());
  if (view.adjusted(16, 16, -16, -16).contains(r)) return;
  m_scrollGlide.stop();
  // Its middle a third of the way down (the zoom box covers the bottom).
  if (r.top() < 16 || r.bottom() > height() - 16) m_scroll.ry() += r.center().y() - height() * 0.3;
  if (r.left() < 16 || r.right() > width() - 16) m_scroll.rx() += r.center().x() - width() / 2;
  clampScroll();
  emit viewChanged();
  refresh();
}

QSizeF InkCanvas::pageSize(int page) const {
  if (page < 0 || page >= int(m_pages.size())) return {};
  return {m_pages[page].w, m_pages[page].h};
}

QImage InkCanvas::regionImage(int page, const QRectF& region, double scale) {
  if (page < 0 || page >= int(m_pages.size()) || region.isEmpty()) return {};
  const Page& p = m_pages[page];
  QImage img = paperWithBackground(p, scale, region).convertToFormat(QImage::Format_ARGB32_Premultiplied);
  const QImage ink = draw::elementsImage(sceneJson(page), scale, region);
  if (!img.isNull() && !ink.isNull()) {
    QPainter painter(&img);
    painter.drawImage(QRectF(0, 0, img.width(), img.height()), ink);
  }
  return img;
}

QJsonObject InkCanvas::liveStroke(int page) const {
  if (m_g.kind != Gesture::Draw || m_g.page != page || m_g.pts.size() < 1 || m_liveHidden) return {};
  QJsonArray pts;
  for (const ink::Point& q : m_g.pts) pts.append(QJsonArray{q.x, q.y, q.p});
  const bool hl = m_g.pen == ink::Pen::Highlighter;
  QJsonObject el{{"id", "live"}, {"z", 0}, {"kind", "stroke"}, {"tool", hl ? "highlighter" : "pen"}, {"color", m_g.color.name(QColor::HexRgb)},
                 {"width", m_g.width}, {"points", pts}};
  if (!hl) el["style"] = m_tool == "shape" ? QStringLiteral("ballpoint") : m_penStyleName;
  if (m_g.dashed) {
    el["dash"] = m_g.dashed == 2 ? "dot" : "dash";
    el["gap"] = m_penGap;
  }
  if (m_g.color.alphaF() < 1) el["opacity"] = m_g.color.alphaF();
  return el;
}

quint64 InkCanvas::pageVersion(int page) const {
  return page >= 0 && page < int(m_pages.size()) ? m_pages[page].version : 0;
}

QString InkCanvas::regionPicture(int page, const QRectF& rect, int maxSide) {
  if (page < 0 || page >= int(m_pages.size())) return {};
  const Page& p = m_pages[page];
  const QRectF r = rect.intersected(QRectF(0, 0, p.w, p.h));
  if (r.isEmpty()) return {};
  const QImage img = regionImage(page, r, std::clamp(double(maxSide) / std::max(r.width(), r.height()), 0.3, 4.0));
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "PNG");
  return "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
}

QVariantMap InkCanvas::selectionArea() {
  if (!m_ed) return {};
  const auto box = m_ed->selectionBounds();
  if (!box) return {};
  return {{"page", m_ed->active}, {"x", box->x}, {"y", box->y}, {"w", box->w}, {"h", box->h}};
}

// --- spelling -------------------------------------------------------------------------

QVariantList InkCanvas::inkWords(int page, bool selectionOnly) {
  QVariantList out;
  if (!m_ed || page < 0 || page >= int(m_pages.size())) return out;
  int n = 1;
  for (const auto& w : m_ed->inkWords(page, selectionOnly))
    out.append(QVariantMap{{"n", n++}, {"x", w.box.x}, {"y", w.box.y}, {"w", w.box.w}, {"h", w.box.h}, {"ids", w.ids}});
  return out;
}

QString InkCanvas::numberedWordsImage(int page, const QVariantList& words) {
  if (page < 0 || page >= int(m_pages.size()) || words.isEmpty()) return {};
  QRectF all;
  for (const QVariant& v : words) {
    const QVariantMap w = v.toMap();
    all = all.united(QRectF(w["x"].toDouble(), w["y"].toDouble(), w["w"].toDouble(), w["h"].toDouble()));
  }
  const Page& p = m_pages[page];
  all = all.adjusted(-28, -28, 16, 16).intersected(QRectF(0, 0, p.w, p.h));
  const double scale = std::clamp(1800.0 / std::max(all.width(), all.height()), 0.6, 3.0);
  // The writing on white (no paper: its lines would only get in the way).
  QImage img(int(std::ceil(all.width() * scale)), int(std::ceil(all.height() * scale)), QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  QPainter painter(&img);
  painter.drawImage(QRectF(0, 0, img.width(), img.height()), draw::elementsImage(sceneJson(page), scale, all));
  painter.setRenderHint(QPainter::Antialiasing);
  QFont f;
  f.setPixelSize(int(std::clamp(14 * scale / 1.5, 12.0, 26.0)));
  f.setBold(true);
  painter.setFont(f);
  for (const QVariant& v : words) {
    const QVariantMap w = v.toMap();
    const QPointF at((w["x"].toDouble() - all.x()) * scale, (w["y"].toDouble() - all.y()) * scale);
    const QString label = QString::number(w["n"].toInt());
    const QRectF r(at.x() - f.pixelSize() * 0.9, at.y() - f.pixelSize() * 1.15, f.pixelSize() * (0.75 + 0.6 * label.size()), f.pixelSize() * 1.25);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(220, 38, 38));
    painter.drawRoundedRect(r, 4, 4);
    painter.setPen(Qt::white);
    painter.drawText(r, Qt::AlignCenter, label);
  }
  painter.end();
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "JPEG", 85);
  return "data:image/jpeg;base64," + QString::fromLatin1(bytes.toBase64());
}

QVariantMap InkCanvas::strokesBox(int page, const QStringList& ids) {
  if (!m_ed || page < 0 || page >= int(m_pages.size()) || ids.isEmpty()) return {};
  QSet<QString> want(ids.begin(), ids.end());
  std::optional<ed::Rect> box;
  int found = 0;
  for (const ed::ElPtr& el : m_ed->load(page))
    if (want.contains(el->id)) {
      const ed::Rect b = ed::bounds(*el);
      box = box ? *ed::unionRects({*box, b}) : b;
      ++found;
    }
  if (found != int(want.size()) || !box) return {};
  return {{"x", box->x}, {"y", box->y}, {"w", box->w}, {"h", box->h}};
}

void InkCanvas::selectElements(int page, const QStringList& ids) {
  if (!m_ed || page < 0 || page >= int(m_pages.size())) return;
  setTool("lasso");
  m_ed->selectIds(page, ids);
  afterCore();
}

void InkCanvas::linkSelection(const QVariantMap& link) {
  if (!m_ed) return;
  m_ed->linkSelection(QJsonObject::fromVariantMap(link));
  afterCore();
}

QVariantList InkCanvas::visibleLinks() {
  QVariantList out;
  if (!m_ed) return out;
  const QRectF view(0, 0, width(), height());
  for (int i = 0; i < int(m_pages.size()); ++i) {
    const Page& p = m_pages[i];
    if (!pageRectToItem(i, QRectF(0, 0, p.w, p.h)).intersects(view)) continue;
    for (const auto& [r, link] : m_ed->linksOn(i)) {
      const QRectF ir = pageRectToItem(i, QRectF(r.x, r.y, r.w, r.h));
      if (!ir.intersects(view.adjusted(-40, -40, 40, 40))) continue;
      out.append(QVariantMap{{"page", i}, {"x", ir.x()}, {"y", ir.y()}, {"w", ir.width()}, {"h", ir.height()}, {"link", link.toVariantMap()}});
    }
  }
  return out;
}

int InkCanvas::pageIndexOf(const QString& pageId) const {
  for (int i = 0; i < int(m_pages.size()); ++i)
    if (m_pages[i].id == pageId) return i;
  return -1;
}

QString InkCanvas::selectedPictureUrl() {
  const QString src = m_ed ? m_ed->selectedImageSrc() : QString();
  if (src.isEmpty()) return {};
  const QString key = QString::number(qHash(src), 16);
  pictureSources().insert(key, src);
  return "image://ink/picture/" + key;
}

bool InkCanvas::cropPicture(const QVariantMap& box, const QVariantList& crop, const QString& mask) {
  if (!m_ed || !m_ed->cropSelectedImage(box.value("x").toDouble(), box.value("y").toDouble(), box.value("w").toDouble(), box.value("h").toDouble(), crop, mask)) return false;
  afterCore();
  return true;
}

void InkCanvas::insertTable(int rows, int cols) {
  if (!m_ed) return;
  finishEdit();
  const int pi = currentPage();
  if (pi < 0 || pi >= int(m_pages.size())) return;
  m_ed->activate(pi);
  const double pw = m_pages[pi].w;
  const double w = std::min(pw * 0.8, std::max(240.0, cols * 120.0));
  const QPointF c = toPageOf(pi, QPointF(width() / 2, height() / 2));
  setTool("lasso");
  m_ed->insertTable(rows, cols, c.x() - w / 2, c.y() - rows * 20, w);
  afterCore();
}

bool InkCanvas::tableFromSelection() {
  if (!m_ed) return false;
  finishEdit();
  if (!m_ed->tableFromSelection()) return false;
  afterCore();
  return true;
}

QVariantMap InkCanvas::tableCell(const QString& id, int row, int col) {
  if (!m_ed) return {};
  QVariantMap cell = m_ed->tableCell(id, row, col);
  if (cell.isEmpty()) return cell;
  cell["rect"] = pageRectToItem(cell.value("page").toInt(), QRectF(cell.value("x").toDouble(), cell.value("y").toDouble(), cell.value("w").toDouble(), cell.value("h").toDouble()));
  cell["zoom"] = m_zoom;
  return cell;
}

bool InkCanvas::setTableCell(const QString& id, int row, int col, const QString& text) {
  if (!m_ed || !m_ed->setTableCell(id, row, col, text)) return false;
  afterCore();
  return true;
}

bool InkCanvas::changeTable(const QString& id, const QString& op, int index) {
  if (!m_ed || !m_ed->changeTableById(id, op, index)) return false;
  afterCore();
  return true;
}

QVariantMap InkCanvas::tableEdges(const QString& id) {
  if (!m_ed) return {};
  const QVariantMap first = m_ed->tableCell(id, 0, 0);
  if (first.isEmpty()) return {};
  const int rows = first.value("rows").toInt(), cols = first.value("cols").toInt(), pi = first.value("page").toInt();
  QVariantList xs, ys, colShares, rowShares;
  double x = 0, y = 0, w = 0, h = 0;
  for (int c = 0; c < cols; ++c) {
    const QVariantMap cell = m_ed->tableCell(id, 0, c);
    if (c == 0) x = cell.value("x").toDouble();
    w += cell.value("w").toDouble();
  }
  for (int r = 0; r < rows; ++r) {
    const QVariantMap cell = m_ed->tableCell(id, r, 0);
    if (r == 0) y = cell.value("y").toDouble();
    h += cell.value("h").toDouble();
  }
  double acc = x;
  xs << pageRectToItem(pi, QRectF(acc, y, 0, 0)).x();
  for (int c = 0; c < cols; ++c) {
    const double cw = m_ed->tableCell(id, 0, c).value("w").toDouble();
    acc += cw;
    colShares << cw / std::max(1e-6, w);
    xs << pageRectToItem(pi, QRectF(acc, y, 0, 0)).x();
  }
  acc = y;
  ys << pageRectToItem(pi, QRectF(x, acc, 0, 0)).y();
  for (int r = 0; r < rows; ++r) {
    const double rh = m_ed->tableCell(id, r, 0).value("h").toDouble();
    acc += rh;
    rowShares << rh / std::max(1e-6, h);
    ys << pageRectToItem(pi, QRectF(x, acc, 0, 0)).y();
  }
  return {{"xs", xs}, {"ys", ys}, {"cols", colShares}, {"rows", rowShares}};
}

bool InkCanvas::setTableSizes(const QString& id, const QVariantList& cols, const QVariantList& rows) {
  if (!m_ed || !m_ed->setTableSizes(id, cols, rows)) return false;
  afterCore();
  return true;
}

QVariantMap InkCanvas::selectionPiece(int maxSide) {
  if (!m_ed) return {};
  finishEdit();
  const QJsonObject piece = m_ed->selectionAsPiece();
  if (piece.isEmpty()) return {};
  const double w = piece["width"].toDouble(), h = piece["height"].toDouble();
  const QJsonArray els = piece["elements"].toArray();
  const double scale = std::min(2.0, double(maxSide) / std::max(1.0, std::max(w, h)));
  const QImage img = draw::elementsImage(els, scale, QRectF(-4, -4, w + 8, h + 8));
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "PNG");
  return {{"elements", QString::fromUtf8(QJsonDocument(els).toJson(QJsonDocument::Compact))}, {"width", w}, {"height", h},
          {"preview", "data:image/png;base64," + QString::fromLatin1(bytes.toBase64())}};
}

void InkCanvas::insertPiece(const QString& elements, double width, double height) {
  if (!m_ed) return;
  finishEdit();
  const QJsonArray els = QJsonDocument::fromJson(elements.toUtf8()).array();
  const int pi = currentPage();
  if (els.isEmpty() || pi < 0 || pi >= int(m_pages.size())) return;
  m_ed->activate(pi);
  const QPointF c = toPageOf(pi, QPointF(this->width() / 2, this->height() / 2));
  const double x = std::clamp(c.x() - width / 2, 0.0, std::max(0.0, double(m_pages[pi].w) - width));
  const double y = std::clamp(c.y() - height / 2, 0.0, std::max(0.0, double(m_pages[pi].h) - height));
  setTool("lasso");
  m_ed->insertPiece(els, x, y);
  afterCore();
}

bool InkCanvas::writeBeside(const QString& text, const QString& font) {
  if (!m_ed) return false;
  finishEdit();
  setTool("lasso");
  const bool done = m_ed->writeBeside(text, font);
  afterCore();
  return done;
}

void InkCanvas::insertText(const QString& text) {
  if (!m_ed || text.trimmed().isEmpty()) return;
  finishEdit();
  const auto box = m_ed->selectionBounds();
  const int pi = box ? activePage() : currentPage();
  if (pi < 0 || pi >= int(m_pages.size())) return;
  m_ed->activate(pi);
  const double pw = m_pages[pi].w, ph = m_pages[pi].h;
  double x, y, w;
  if (box) {
    x = box->x;
    y = std::min(box->y + box->h + 16, ph - 40);
    w = std::max(box->w, pw * 0.5);
  } else {
    const QPointF c = toPageOf(pi, QPointF(width() / 2, height() / 2));
    w = pw * 0.7;
    x = std::max(20.0, c.x() - w / 2);
    y = std::max(20.0, c.y() - 60);
  }
  setTool("lasso");
  m_ed->insertText(text.trimmed(), x, y, std::min(w, pw - x - 20));
  afterCore();
}

QString InkCanvas::selectionText() { return m_ed ? m_ed->selectedText() : QString(); }

bool InkCanvas::applyFontToInk(const QString& text, const QString& font, bool keepInk) {
  const bool done = m_ed && m_ed->inkToText(text, font, keepInk);
  syncSelection();
  afterCore();
  if (done) emit message("Handwriting replaced with text. Use the font menu above it to try other fonts, or Ctrl+Z to undo", false);
  return done;
}

void InkCanvas::insertImage(const QString& fileUrl) {
  const QString path = QUrl(fileUrl).isLocalFile() ? QUrl(fileUrl).toLocalFile() : fileUrl;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return emit message("Couldn't open that picture", true);
  const QByteArray bytes = f.readAll();
  if (bytes.size() > 15 * 1024 * 1024) return emit message("Images larger than 15 MB are not supported", true);
  QImage img;
  if (!img.loadFromData(bytes)) return emit message("That file isn't a picture Inkleaf can read", true);
  QString mime = "image/png";
  const QByteArray fmt = QImageReader::imageFormat(path);
  if (fmt == "jpeg" || fmt == "jpg") mime = "image/jpeg";
  else if (fmt == "gif") mime = "image/gif";
  else if (fmt == "webp") mime = "image/webp";
  const QString src = "data:" + mime + ";base64," + QString::fromLatin1(bytes.toBase64());
  const int pi = currentPage();
  if (!m_ed) return;
  m_ed->activate(pi);
  const QPointF c = toPageOf(pi, QPointF(width() / 2, height() / 2));
  setTool("lasso");
  m_ed->insertImage(src, img.width(), img.height(), c.x(), c.y());
  afterCore();
}

void InkCanvas::updateEdit(const QString& text) {
  if (m_ed) m_ed->updateEdit(text);
  m_editing["text"] = text;
}

QVariantMap InkCanvas::continueList(const QString& text, int caret) { return ed::continueListFor(text, caret); }

void InkCanvas::editFormat(const QVariantMap& change) {
  if (m_ed) m_ed->applyFormat(change);
  if (change.contains("font")) m_textFont = change["font"].toString();
  syncEditing();
  emit toolStateChanged();
}

void InkCanvas::finishEdit() {
  if (m_ed) m_ed->finishTextEdit();
  syncEditing();
  afterCore();
}

// --- layout and view ---------------------------------------------------------------------

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
  if (m_ed) m_ed->viewWidth = n.width();
  if (m_fitPending && n.width() > 0 && !m_pages.empty()) fitWidth();
  clampScroll();
  emit viewChanged();
  refresh();
}

void InkCanvas::clampScroll() {
  const qreal maxX = std::max<qreal>(0, m_maxPageW * m_zoom + 2 * kPad - width());
  const qreal maxY = std::max<qreal>(0, m_contentH * m_zoom + kPadTop + kPadBottom - height());
  m_scroll.setX(std::clamp<qreal>(m_scroll.x(), 0, maxX));
  m_scroll.setY(std::clamp<qreal>(m_scroll.y(), 0, maxY));
}

/// Where page-unit content x=0 of the widest page sits on screen.
QPointF InkCanvas::origin() const {
  const qreal contentW = m_maxPageW * m_zoom;
  const qreal x = contentW + 2 * kPad <= width() ? (width() - contentW) / 2 : kPad - m_scroll.x();
  return {x, kPadTop - m_scroll.y()};
}

QPointF InkCanvas::toPage(QPointF s, int* pageIndex) const {
  const QPointF o = origin();
  const QPointF c((s.x() - o.x()) / m_zoom, (s.y() - o.y()) / m_zoom);
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

QPointF InkCanvas::toPageOf(int i, QPointF s) const {
  if (i < 0 || i >= int(m_pages.size())) return {};
  const QPointF o = origin();
  const Page& p = m_pages[i];
  return {(s.x() - o.x()) / m_zoom - (m_maxPageW - p.w) / 2, (s.y() - o.y()) / m_zoom - p.top};
}

QRectF InkCanvas::pageRectToItem(int i, const QRectF& r) const {
  if (i < 0 || i >= int(m_pages.size())) return {};
  const QPointF o = origin();
  const Page& p = m_pages[i];
  const double x = o.x() + ((m_maxPageW - p.w) / 2 + r.x()) * m_zoom;
  const double y = o.y() + (p.top + r.y()) * m_zoom;
  return QRectF(x, y, r.width() * m_zoom, r.height() * m_zoom);
}

int InkCanvas::pageAt(QPointF s) const {
  int i = -1;
  const QPointF p = toPage(s, &i);
  if (i < 0) return -1;
  const Page& pg = m_pages[i];
  return (p.x() >= 0 && p.y() >= 0 && p.x() <= pg.w && p.y() <= pg.h) ? i : -1;
}

int InkCanvas::pageNear(QPointF s, double within) const {
  int best = -1;
  double bestD = within;
  for (int i = 0; i < int(m_pages.size()); ++i) {
    const QRectF r = pageRectToItem(i, QRectF(0, 0, m_pages[i].w, m_pages[i].h));
    const double dx = std::max({r.left() - s.x(), 0.0, s.x() - r.right()});
    const double dy = std::max({r.top() - s.y(), 0.0, s.y() - r.bottom()});
    const double d = std::hypot(dx, dy);
    if (d <= bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

int InkCanvas::currentPage() const {
  int i = -1;
  toPage(QPointF(width() / 2, height() / 2), &i);
  return std::max(0, i);
}

int InkCanvas::activePage() const { return m_ed ? m_ed->active : 0; }

void InkCanvas::zoomAt(qreal z, QPointF anchor) {
  z = std::clamp(z, kMinZoom, kMaxZoom);
  const QPointF o = origin();
  const QPointF content((anchor.x() - o.x()) / m_zoom, (anchor.y() - o.y()) / m_zoom);
  m_zoom = z;
  m_scroll.setX(kPad + content.x() * m_zoom - anchor.x());
  m_scroll.setY(kPadTop + content.y() * m_zoom - anchor.y());
  clampScroll();
  pushToolState();
  m_overlayDirty = true;
  m_settle.start();
  emit viewChanged();
  refresh();
}

void InkCanvas::zoomBy(qreal f) { zoomAt(m_zoom * f, QPointF(width() / 2, height() / 2)); }
void InkCanvas::setZoomLevel(qreal z) { zoomAt(z, QPointF(width() / 2, height() / 2)); }

void InkCanvas::fitWidth() {
  if (width() <= 0 || m_pages.empty()) return;
  m_fitPending = false;
  const qreal z = std::min<qreal>(1.6, (width() - 2 * kPad) / m_maxPageW);
  const qreal topContent = m_scroll.y() / m_zoom;
  m_zoom = std::clamp(z, kMinZoom, kMaxZoom);
  m_scroll = QPointF(0, topContent * m_zoom);
  clampScroll();
  pushToolState();
  m_overlayDirty = true;
  m_settle.start();
  emit viewChanged();
  refresh();
}

void InkCanvas::goToPage(int i) {
  if (i < 0 || i >= int(m_pages.size())) return;
  m_scrollGlide.stop();
  m_scroll.setY(m_pages[i].top * m_zoom);
  clampScroll();
  emit viewChanged();
  refresh();
}

double InkCanvas::renderScale() const {
  const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
  return bucket(m_zoom * dpr);
}

void InkCanvas::refresh() {
  polish();
  update();
  m_prefetch.start(180);
}

bool InkCanvas::prefetchStep() {
  if (!m_ed || m_pages.empty() || m_g.kind != Gesture::None || m_scrollGlide.isActive()) return false;
  const QPointF o = origin();
  const qreal screen = height() / m_zoom;
  const qreal viewTop = -o.y() / m_zoom, viewBottom = (height() - o.y()) / m_zoom;
  const qreal reach = std::max<qreal>(screen * 10, 8000);
  // The nearest page not ready yet: near the view, loaded and drawn; further
  // off, only loaded (its pictures are made as it comes near).
  int best = -1;
  qreal bestDist = 0;
  for (int i = 0; i < int(m_pages.size()); ++i) {
    const Page& p = m_pages[i];
    const qreal d = p.top > viewBottom ? p.top - viewBottom : p.top + p.h < viewTop ? viewTop - (p.top + p.h) : 0;
    bool ready = p.synced;
    if (ready && d <= reach) {
      ready = p.paperScale > 0;
      for (const auto& it : p.items)
        if (ready && !it->mesh && it->scale <= 0) ready = false;
    }
    if (ready) continue;
    if (best < 0 || d < bestDist) {
      best = i;
      bestDist = d;
    }
  }
  if (best < 0) return false;
  static const bool trace = qEnvironmentVariableIsSet("INKLEAF_TRACE");
  Page& p = m_pages[best];
  if (!p.synced) syncPage(best);
  if (bestDist > reach) return true;
  const double s = renderScale();
  if (p.paperScale <= 0) {
    const double full = std::min(s, kMaxTexture / double(std::max(p.w, p.h)));
    p.paperImg = paperWithBackground(p, full, {});
    p.paperScale = full;
    p.paperDirty = true;
  }
  for (auto& it : p.items) {
    if (it->mesh || it->scale > 0) continue;
    const double cap = kMaxTexture / std::max(1.0, std::max(it->rect.width(), it->rect.height()));
    const double want = std::min(s, cap);
    it->image = draw::elementsImage(QJsonArray{it->data}, want, it->rect);
    it->scale = want;
    it->imageDirty = true;
    p.version++;
  }
  update();
  return true;
}

void InkCanvas::updatePolish() {
  // GUI thread, before the frame: elements and pictures for pages coming into view.
  if (!m_ed) return;
  static const bool trace = qEnvironmentVariableIsSet("INKLEAF_TRACE");
  QElapsedTimer pt;
  pt.start();
  struct PolishDone {
    QElapsedTimer& t;
    ~PolishDone() {
      if (trace && t.nsecsElapsed() > 4e6) qInfo("polish %.1f ms", t.nsecsElapsed() / 1e6);
    }
  } polishDone{pt};
  const QPointF o = origin();
  const qreal screen = height() / m_zoom;
  const qreal top = -o.y() / m_zoom - screen, bottom = (height() - o.y()) / m_zoom + screen;
  bool any = false;
  for (int i = 0; i < int(m_pages.size()); ++i) {
    Page& p = m_pages[i];
    const bool near = p.top + p.h >= top && p.top <= bottom;
    if (near && !p.synced) {
      syncPage(i);
      any = true;
    }
  }
  if (any) update();
  // Missing pictures at once; sharper ones once the zoom settles.
  const qint64 beforePictures = pt.nsecsElapsed();
  renderPictures(m_settle.isActive());
  if (trace && pt.nsecsElapsed() - beforePictures > 4e6) qInfo("pictures %.1f ms", (pt.nsecsElapsed() - beforePictures) / 1e6);
  if (m_overlayDirty) {
    QElapsedTimer ot;
    ot.start();
    struct Done {
      QElapsedTimer& t;
      ~Done() {
        if (qEnvironmentVariableIsSet("INKLEAF_TRACE") && t.nsecsElapsed() > 3e6) qInfo("overlay %.1f ms", t.nsecsElapsed() / 1e6);
      }
    } done{ot};
    m_overlayDirty = false;
    m_overlayImg = QImage();
    m_overlayPage = -1;
    const int pi = m_ed->active;
    const auto b = m_ed->overlayBounds();
    // (Not while something covers the selection, as cropping a picture does.)
    if (b && !m_hideSelection && pi >= 0 && pi < int(m_pages.size())) {
      const Page& pg = m_pages[pi];
      // Only what's on screen.
      const QRectF view(toPageOf(pi, QPointF(0, 0)), toPageOf(pi, QPointF(width(), height())));
      const QRectF r = QRectF(b->x, b->y, b->w, b->h).intersected(view.adjusted(-20, -20, 20, 20));
      Q_UNUSED(pg);
      if (!r.isEmpty()) {
        const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
        const double s = m_zoom * dpr;
        Canvas canvas(int(std::ceil(r.width() * s)), int(std::ceil(r.height() * s)));
        canvas.setTransform(s, 0, 0, s, -r.x() * s, -r.y() * s);
        m_ed->drawOverlay(canvas, pi);
        m_overlayImg = canvas.image();
        m_overlayRect = r;
        m_overlayPage = pi;
      }
    }
    update();
  }
}

QImage InkCanvas::paperWithBackground(const Page& p, double scale, QRectF region) {
  QImage img = draw::paperImage(p.tmpl, p.paper, p.w, p.h, scale, region);
  // An imported PDF page goes over the paper, as in the web app.
  const QString asset = p.meta["backgroundAsset"].toString();
  if (!asset.isEmpty() && !p.meta["backgroundPage"].isNull()) {
    const QRectF r = region.isEmpty() ? QRectF(0, 0, p.w, p.h) : region;
    // The PDF page fills the page (its size in units may differ slightly).
    const QSizeF ps = PdfDocs::instance().pageSize(asset, p.meta["backgroundPage"].toInt());
    if (!ps.isEmpty()) {
      const double fit = p.w / ps.width();
      const QImage bg = PdfDocs::instance().render(asset, p.meta["backgroundPage"].toInt(), scale * fit, QRectF(r.x() / fit, r.y() / fit, r.width() / fit, r.height() / fit));
      QPainter painter(&img);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      painter.drawImage(QRectF(0, 0, img.width(), img.height()), bg);
    }
  }
  return img;
}

void InkCanvas::renderPictures(bool missingOnly) {
  if (m_pages.empty()) return;
  const double s = renderScale();
  const QPointF o = origin();
  const qreal viewTop = -o.y() / m_zoom, viewBottom = (height() - o.y()) / m_zoom;
  const qreal screen = height() / m_zoom;
  QElapsedTimer budget;
  budget.start();
  bool more = false;
  for (int i = 0; i < int(m_pages.size()); ++i) {
    Page& p = m_pages[i];
    const bool visible = p.top + p.h >= viewTop && p.top <= viewBottom;
    const bool near = p.top + p.h >= viewTop - screen && p.top <= viewBottom + screen;
    if (!near) continue;
    if (budget.elapsed() > 40 && !visible) {
      more = true;
      continue;
    }
    // The whole page, as sharp as fits one picture.
    const double full = std::min(s, kMaxTexture / double(std::max(p.w, p.h)));
    if (p.paperScale != full && !(missingOnly && p.paperScale > 0)) {
      p.paperImg = paperWithBackground(p, full, {});
      p.paperScale = full;
      p.paperDirty = true;
    }
    // Zoomed in further than that: the visible part, sharp.
    if (full < s * 0.99 && visible && !missingOnly) {
      const QRectF view(toPageOf(i, QPointF(0, 0)), toPageOf(i, QPointF(width(), height())));
      const QRectF want = view.intersected(QRectF(0, 0, p.w, p.h));
      if (p.detailScale != s || !p.detailRect.contains(want)) {
        const QRectF r = want.adjusted(-want.width() * 0.25, -want.height() * 0.25, want.width() * 0.25, want.height() * 0.25).intersected(QRectF(0, 0, p.w, p.h));
        p.detailImg = paperWithBackground(p, s, r);
        p.detailRect = r;
        p.detailScale = s;
        p.detailDirty = true;
      }
    } else if (p.detailScale != 0 && full >= s * 0.99) {
      p.detailScale = 0;
      p.detailImg = QImage();
      p.detailDirty = true;
    }
    // Element pictures.
    for (auto& it : p.items) {
      if (it->mesh) continue;
      const double cap = kMaxTexture / std::max(1.0, std::max(it->rect.width(), it->rect.height()));
      const double want = std::min(s, cap);
      if (it->scale == want || (missingOnly && it->scale > 0)) continue;
      if (budget.elapsed() > 40 && !visible && it->scale > 0) {
        more = true;
        continue;
      }
      it->image = draw::elementsImage(QJsonArray{it->data}, want, it->rect);
      it->scale = want;
      it->imageDirty = true;
      p.version++;
    }
  }
  if (more) QTimer::singleShot(0, this, [this] { refresh(); });
  update();
}

// --- input -------------------------------------------------------------------------------

bool InkCanvas::navigatesOnly(bool stylus) const { return m_penOnly && !stylus && m_tool != "laser"; }

void InkCanvas::press(QPointF s, float pressure, bool stylus, const QString& toolOverride, bool touch, Qt::KeyboardModifiers mods) {
  if (!m_ed) return;
  m_scrollGlide.stop();
  m_g = Gesture();
  // A press on the page while the options drawer is open closes it. A stroke
  // started that way still writes; only a plain tap is taken as "close".
  m_dismissing = m_dismissOnPress;
  m_dismissFrom = s;
  if (m_dismissOnPress) emit dismissed();
  // A view-only page (the same notebook beside itself) only scrolls.
  const QString tool = m_readOnly ? QStringLiteral("hand") : toolOverride.isEmpty() ? m_tool : toolOverride;
  if (!hasActiveFocus()) forceActiveFocus();
  m_g.lastScreen = s;
  m_g.touch = touch;
  m_g.stylus = stylus;
  if (tool == "hand" || navigatesOnly(stylus)) {
    m_g.kind = Gesture::Pan;
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  if (tool == "laser") {
    m_g.kind = Gesture::Laser;
    if (m_laserMode == "trail") {
      if (m_laserUp >= 0 && nowMs() - m_laserUp > kLaserHold) m_laser.clear();
      m_laser.push_back({s});
    }
    m_laserDot = s;
    m_laserUp = -1;
    m_laserTick.stop();
    emit laserChanged();
    update();
    return;
  }
  int pi = pageAt(s);
  // The eraser and the lasso may start just off the paper (to reach ink at its edge).
  if (pi < 0 && (tool == "eraser" || tool == "lasso")) pi = pageNear(s, 160);
  if (pi < 0) {
    if (m_tool == "lasso") if (m_ed) m_ed->clearSelection();
    if (m_ed) m_ed->finishTextEdit();
    syncEditing();
    afterCore();
    return;
  }
  if (!m_pages[pi].synced) syncPage(pi);
  const QPointF p = toPageOf(pi, s);
  m_g.page = pi;
  m_ed->scratchOut = m_scratchOut;
  const QString r = m_ed->down(pi, p.x(), p.y(), pressure, nowMs(), tool, bool(mods & Qt::ShiftModifier));
  m_g.kind = Gesture::Core;
  if (r == "draw") {
    m_g.kind = Gesture::Draw;
    const bool hl = tool == "highlighter";
    const bool shape = tool == "shape";
    m_g.pen = hl ? ink::Pen::Highlighter : shape ? ink::Pen::Ballpoint : ink::penFromName(m_penStyleName, false);
    m_g.color = Canvas::parseColor(m_colors.value(tool).toString());
    m_g.width = float(m_widths.value(tool, 3).toDouble());
    if (m_g.pen == ink::Pen::Tape) m_g.width = ink::tapeWidth(m_g.width);
    m_g.dashed = m_g.pen == ink::Pen::Construction ? (m_penDash == "dot" ? 2 : 1) : 0;
    const float pr = ink::speedSensitive(m_g.pen) ? m_g.speed.next(float(p.x()), float(p.y()), nowMs(), stylus ? pressure : -1.f) : (stylus ? pressure : 0.5f);
    m_g.pts.push_back({float(p.x()), float(p.y()), pr});
    m_g.mesh = std::make_shared<ink::Mesh>();
    ink::strokeMesh(m_g.pts, m_g.pen, m_g.width, shape ? 0.f : float(m_smoothing), *m_g.mesh, m_g.dashed, float(m_penGap));
    m_liveChanged = true;
  } else if (r == "edit") {
    m_g = Gesture();
    syncEditing();
  } else if (r == "cell") {
    m_g = Gesture();
    emit cellEditRequested(m_ed->takeCellRequest());
  }
  m_rubbing = tool == "eraser";
  setWriting(m_g.kind == Gesture::Draw || m_g.kind == Gesture::Core);
  setHover(s);
  afterCore();
}

void InkCanvas::drag(QPointF s, float pressure, bool stylus, Qt::KeyboardModifiers mods) {
  // Moved: not the tap that closes the drawer, a stroke.
  if (m_dismissing && QLineF(m_dismissFrom, s).length() >= 6) m_dismissing = false;
  switch (m_g.kind) {
    case Gesture::None:
      return;
    case Gesture::Pan:
      m_scroll -= s - m_g.lastScreen;
      m_g.lastScreen = s;
      clampScroll();
      emit viewChanged();
      refresh();
      return;
    case Gesture::Laser:
      m_laserDot = s;
      if (m_laserMode == "trail" && !m_laser.empty()) {
        // Points closer than a pixel add nothing but work.
        auto& run = m_laser.back();
        if (run.empty() || QLineF(run.back(), s).length() >= 1.0) run.push_back(s);
      }
      emit laserChanged();
      return;
    case Gesture::Core: {
      const QPointF p = toPageOf(m_g.page, s);
      m_ed->move(p.x(), p.y(), pressure, nowMs(), bool(mods & Qt::ShiftModifier), bool(mods & Qt::AltModifier));
      setHover(s);
      afterCore();
      return;
    }
    case Gesture::Draw: {
      QPointF p = toPageOf(m_g.page, s);
      if (m_liveHidden) {
        // A refined shape follows the pen (rotate, resize, move).
        m_ed->move(p.x(), p.y(), pressure, nowMs(), bool(mods & Qt::ShiftModifier), false);
        afterCore();
        return;
      }
      const ink::Point& last = m_g.pts.back();
      // Lazy pen: the ink trails the tip by at most about a screen pixel.
      const float lazy = float(kLazyPen / (m_zoom * m_inputMag));
      const float d = float(std::hypot(p.x() - last.x, p.y() - last.y));
      if (d <= lazy) return;
      p = QPointF(last.x + (p.x() - last.x) * (d - lazy) / d, last.y + (p.y() - last.y) * (d - lazy) / d);
      if (std::hypot(p.x() - last.x, p.y() - last.y) < 0.4 / (m_zoom * m_inputMag)) return;
      const float pr = ink::speedSensitive(m_g.pen) ? m_g.speed.next(float(p.x()), float(p.y()), nowMs(), stylus ? pressure : -1.f) : (stylus ? pressure : 0.5f);
      m_g.pts.push_back({float(p.x()), float(p.y()), pr});
      ink::strokeMesh(m_g.pts, m_g.pen, m_g.width, m_g.pen == ink::Pen::Ballpoint && m_tool == "shape" ? 0.f : float(m_smoothing), *m_g.mesh, m_g.dashed, float(m_penGap));
      m_liveChanged = true;
      m_ed->move(p.x(), p.y(), pr, nowMs(), bool(mods & Qt::ShiftModifier), false);
      afterCore();
      return;
    }
  }
}

void InkCanvas::setHover(QPointF p) {
  if (p == m_hover) return;
  m_hover = p;
  emit pointerChanged();
}

void InkCanvas::release() {
  setWriting(false);
  const Gesture::Kind k = m_g.kind;
  if (m_rubbing) {
    m_rubbing = false;
    emit pointerChanged();
  }
  if (k == Gesture::Pan) {
    m_g = Gesture();
    updateCursor();
    return;
  }
  if (k == Gesture::Laser) {
    m_g = Gesture();
    m_laserUp = nowMs();
    if (m_laserMode == "dot") m_laser.clear();
    m_laserTick.start();  // the fade
    emit laserChanged();
    return;
  }
  if ((k == Gesture::Core || k == Gesture::Draw) && m_ed) {
    // The tap that closed the drawer: not a dot, not a new text box.
    if (m_dismissing) m_ed->cancel();
    else m_ed->up();
  }
  m_dismissing = false;
  m_g = Gesture();
  m_liveChanged = true;
  afterCore();
  if (m_growable) QTimer::singleShot(0, this, &InkCanvas::growIfNeeded);
}

void InkCanvas::growIfNeeded() {
  if (!m_growable || !m_ed || m_g.kind != Gesture::None) return;
  const int pi = activePage();
  if (pi < 0 || pi >= int(m_pages.size())) return;
  double right = 0, bottom = 0;
  for (const ed::ElPtr& el : m_ed->load(pi)) {
    const ed::Rect b = ed::bounds(*el);
    right = std::max(right, b.x + b.w);
    bottom = std::max(bottom, b.y + b.h);
  }
  // Writing near the right or bottom edge: room for more, in steps.
  const double w = m_pages[pi].w, h = m_pages[pi].h, near = 360, step = 400;
  if (right < w - near && bottom < h - near) return;
  const double nw = right >= w - near ? std::ceil((right + 1200) / step) * step : w;
  const double nh = bottom >= h - near ? std::ceil((bottom + 900) / step) * step : h;
  flush();
  QString err;
  InkleafCore::instance()->callJson("setPageSize", {{"pageId", m_pages[pi].id}, {"width", nw}, {"height", nh}}, &err);
  if (!err.isEmpty()) return emit message(err, true);
  const QPointF scroll = m_scroll;
  reloadPages(false);
  m_scroll = scroll;
  clampScroll();
  emit viewChanged();
  refresh();
}

bool InkCanvas::eventFilter(QObject* watched, QEvent* event) {
  const auto type = event->type();
  if (watched != m_window || !(type == QEvent::TabletPress || type == QEvent::TabletMove || type == QEvent::TabletRelease)) return false;
  auto* te = static_cast<QTabletEvent*>(event);
  const QPointF local = mapFromScene(te->position());
  const bool inside = contains(local);
  m_lastPenMs = nowMs();
  const bool eraserEnd = m_eraserEndOn && te->pointerType() == QPointingDevice::PointerType::Eraser;
  const bool side = te->buttons() & (Qt::RightButton | Qt::MiddleButton);
  QString override;
  if (eraserEnd) override = "eraser";
  else if (side && m_sideButton != "none") override = m_sideButton;
  if (type == QEvent::TabletPress) {
    if (!inside || !isVisible()) return false;
    emit penUsed();
    // Not taken here: Qt Quick delivers it to whatever is on top at that
    // point (an open menu, the tool drawer, a button, the update card) as
    // a mouse press. Only when that press reaches this item does the
    // stroke start (mousePressEvent), with this pen's pressure.
    m_pendingPen = {local, curved(te->pressure()), override, te->modifiers(), nowMs()};
    return false;
  }
  if (type == QEvent::TabletMove) {
    if (m_g.kind == Gesture::None || !m_g.stylus) {
      setHover(inside ? local : QPointF(-1, -1));
      if (m_tool == "laser") {
        m_laserDot = inside ? local : QPointF(-1, -1);
        emit laserChanged();
      }
      return false;
    }
    setHover(local);
    if (m_g.kind == Gesture::Core) {
      const bool queued = m_pendingMove.waiting;
      m_pendingMove = {local, curved(te->pressure()), te->modifiers(), true};
      if (!queued) QMetaObject::invokeMethod(this, &InkCanvas::flushPendingMove, Qt::QueuedConnection);
    } else {
      drag(local, curved(te->pressure()), true, te->modifiers());
    }
    te->accept();
    return true;
  }
  m_pendingPen.at = -1e9;
  if (m_g.kind == Gesture::None || !m_g.stylus) return false;
  flushPendingMove();
  release();
  // Not accepted: Qt Quick still gets the matching mouse release, so the
  // press it gave this item ends (mouseReleaseEvent ignores it).
  return false;
}

void InkCanvas::flushPendingMove() {
  if (!m_pendingMove.waiting) return;
  m_pendingMove.waiting = false;
  if (m_g.kind == Gesture::Core) drag(m_pendingMove.local, m_pendingMove.pressure, true, m_pendingMove.mods);
}

void InkCanvas::mousePressEvent(QMouseEvent* e) {
  // The pen press the window just saw, which nothing on top took: a stroke.
  // (Matched by time and place: some systems mark the mouse press made from
  // a pen as the pen, others as the mouse.)
  const bool fromPen = nowMs() - m_pendingPen.at < 500 && QLineF(m_pendingPen.local, e->position()).length() < 32;
  if (!fromPen && e->device() && e->device()->type() == QInputDevice::DeviceType::Stylus) return e->ignore();
  if (fromPen) {
    const PendingPen pen = m_pendingPen;
    m_pendingPen.at = -1e9;
    if (m_g.touch && m_palmRejection) {
      if (m_ed) m_ed->cancel();
      m_g = Gesture();
    }
    press(pen.local, pen.pressure, true, pen.override, false, pen.mods);
    return;
  }
  forceActiveFocus();
  if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
    m_g = Gesture();
    m_g.kind = Gesture::Pan;
    m_g.lastScreen = e->position();
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  press(e->position(), 0.5f, false, QString(), false, e->modifiers());
}

void InkCanvas::mouseMoveEvent(QMouseEvent* e) {
  // A pen's strokes follow its own (tablet) moves, with pressure.
  if (m_g.stylus || (e->device() && e->device()->type() == QInputDevice::DeviceType::Stylus)) return e->ignore();
  setHover(e->position());
  drag(e->position(), 0.5f, false, e->modifiers());
}

void InkCanvas::mouseReleaseEvent(QMouseEvent* e) {
  // A pen's stroke ended with its tablet release (just before this one).
  if (m_g.stylus || m_g.kind == Gesture::None || (e->device() && e->device()->type() == QInputDevice::DeviceType::Stylus)) return;
  release();
}

void InkCanvas::hoverMoveEvent(QHoverEvent* e) {
  setHover(e->position());
  if (m_tool == "laser") {
    m_laserDot = e->position();
    emit laserChanged();
  }
  if (m_tool == "lasso" && m_ed) {
    int pi = -1;
    const QPointF p = toPage(e->position(), &pi);
    if (pi == activePage()) {
      const QString c = m_ed->cursorAt(p.x(), p.y());
      setCursor(c == "move" ? Qt::SizeAllCursor : c == "ns" ? Qt::SizeVerCursor : c == "ew" ? Qt::SizeHorCursor : c == "nwse" ? Qt::SizeFDiagCursor : c == "nesw" ? Qt::SizeBDiagCursor
                : c == "rotate" || c == "crosshair" ? Qt::CrossCursor : Qt::ArrowCursor);
    }
  }
}

void InkCanvas::hoverLeaveEvent(QHoverEvent*) {
  setHover(QPointF(-1, -1));
  m_laserDot = QPointF(-1, -1);
  emit laserChanged();
  update();
}

void InkCanvas::wheelEvent(QWheelEvent* e) {
  if (e->modifiers() & Qt::ControlModifier) {
    zoomAt(m_zoom * std::exp(e->angleDelta().y() * 0.0025), e->position());
    return;
  }
  // Touchpads (and other smooth scrolling) move by their pixels; a mouse
  // wheel by a step per notch. Which it is: the platform marks finger and
  // smooth scrolls as synthesized (Wayland) or the device is a touchpad
  // (X11). Newer Qt also gives a wheel pixel deltas; they're ignored.
  const QPoint px = e->pixelDelta();
  const bool touchpad = !px.isNull() && (e->source() == Qt::MouseEventSynthesizedBySystem || (e->pointingDevice() && e->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad));
  // A notch moves a step; notches that follow quickly (a flick of the wheel)
  // move a little further each: a flick of four notches passes about a page.
  // Counted in notches (120 units), so high-resolution wheels, which send
  // many small events per notch, scroll the same.
  const double now = nowMs();
  if (!touchpad) {
    if (now - m_lastWheel > 220) m_wheelNotches = 0;
    m_wheelNotches += std::abs(e->angleDelta().y() ? e->angleDelta().y() : e->angleDelta().x()) / 120.0;
    m_wheelBoost = std::min(3.5, std::pow(1.25, std::max(0.0, m_wheelNotches - 1)));
    m_lastWheel = now;
  }
  const QPointF d = touchpad ? QPointF(px) : QPointF(e->angleDelta()) / 120.0 * 200.0 * m_wheelBoost;
  const QPointF step = (e->modifiers() & Qt::ShiftModifier) ? QPointF(d.y(), 0) : d;
  static const bool trace = qEnvironmentVariableIsSet("INKLEAF_TRACE");
  if (trace) qInfo("wheel angle %d px %d phase %d touchpad %d boost %.2f step %.0f", e->angleDelta().y(), px.y(), int(e->phase()), int(touchpad), m_wheelBoost, step.y());
  if (touchpad) {
    m_scrollGlide.stop();
    m_scroll -= step;
    clampScroll();
    m_overlayDirty = true;
    emit viewChanged();
    refresh();
    return;
  }
  // A notch glides there over a few frames (several notches add up).
  if (!m_scrollGlide.isActive()) {
    m_glideTarget = m_scroll;
    m_glideLast = now - 8;
  }
  m_glideTarget -= step;
  // Not past the ends (else the glide would wait there).
  const QPointF keep = m_scroll;
  m_scroll = m_glideTarget;
  clampScroll();
  m_glideTarget = m_scroll;
  m_scroll = keep;
  if (!m_scrollGlide.isActive()) m_scrollGlide.start();
}

void InkCanvas::touchEvent(QTouchEvent* e) {
  const auto& pts = e->points();
  trackFingerTap(e);
  if (pts.size() >= 2) {
    // Two fingers: pinch to zoom and move (a second finger cancels a touch stroke).
    if (m_g.kind == Gesture::Draw || m_g.kind == Gesture::Core) {
      if (m_ed) m_ed->cancel();
      m_g = Gesture();
      afterCore();
    }
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
  if (p.state() == QEventPoint::Pressed) {
    if (m_palmRejection && nowMs() - m_lastPenMs < 1500) {
      m_g = Gesture();
      e->accept();
      return;
    }
    press(p.position(), 0.5f, false, QString(), true, e->modifiers());
  } else if (p.state() == QEventPoint::Updated) {
    drag(p.position(), 0.5f, false, e->modifiers());
  } else if (p.state() == QEventPoint::Released) {
    release();
  }
  e->accept();
}

void InkCanvas::trackFingerTap(QTouchEvent* e) {
  const auto& pts = e->points();
  const double now = nowMs();
  if (e->type() == QEvent::TouchBegin) m_tap = FingerTap{now, 0, false, {}};
  m_tap.fingers = std::max<int>(m_tap.fingers, int(pts.size()));
  for (const QEventPoint& p : pts) {
    if (p.state() == QEventPoint::Pressed || !m_tap.at.contains(p.id())) m_tap.at.insert(p.id(), p.position());
    else if (QLineF(m_tap.at.value(p.id()), p.position()).length() > 14) m_tap.moved = true;
  }
  if (e->type() != QEvent::TouchEnd && e->type() != QEvent::TouchCancel) return;
  // A quick tap of two or three fingers that didn't move: undo or redo. Not
  // with the pen just used (a palm resting) and not for a longer touch.
  const bool tap = e->type() == QEvent::TouchEnd && m_fingerTaps && !m_readOnly && !m_tap.moved && now - m_tap.start < 320 &&
                   (m_tap.fingers == 2 || m_tap.fingers == 3) && now - m_lastPenMs > 1500;
  const int fingers = m_tap.fingers;
  m_tap = FingerTap{};
  if (!tap) return;
  if (fingers == 2) {
    if (!canUndo()) return;
    undo();
    emit fingerTapped(false);
  } else {
    if (!canRedo()) return;
    redo();
    emit fingerTapped(true);
  }
}

// --- scene graph --------------------------------------------------------------------------

QSGNode* InkCanvas::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  static const bool profile = qEnvironmentVariableIsSet("INKLEAF_PROFILE");
  QElapsedTimer timer;
  if (profile) timer.start();
  QQuickWindow* win = window();
  // root → content transform → pages; live ink; overlay; eraser ring; laser (screen space).
  auto* root = old;
  QSGTransformNode *content, *liveT, *overlayT, *rulerT;
  QSGGeometryNode *live, *liveGlow, *ring, *laser;
  QSGClipNode* liveClip;
  if (!root) {
    root = new QSGNode;
    content = new QSGTransformNode;
    liveT = new QSGTransformNode;
    overlayT = new QSGTransformNode;
    rulerT = new QSGTransformNode;
    live = meshNode({}, Qt::black);
    liveGlow = meshNode({}, Qt::transparent);
    ring = meshNode({}, QColor(100, 100, 100, 200));
    laser = meshNode({}, Qt::red);
    static_cast<QSGFlatColorMaterial*>(laser->material())->setFlag(QSGMaterial::Blending);
    // The stroke being written is clipped to its page (ink stays on the paper).
    liveClip = new QSGClipNode;
    liveClip->setIsRectangular(true);
    auto* clipGeom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
    liveClip->setGeometry(clipGeom);
    liveClip->setFlag(QSGNode::OwnsGeometry);
    liveT->appendChildNode(liveClip);
    liveClip->appendChildNode(liveGlow);
    liveClip->appendChildNode(live);
    root->appendChildNode(content);
    root->appendChildNode(liveT);
    root->appendChildNode(overlayT);
    root->appendChildNode(rulerT);
    root->appendChildNode(ring);
    root->appendChildNode(laser);
    m_nodes.clear();
    m_rebuildAll = true;
    m_overlayTex = nullptr;
  } else {
    content = static_cast<QSGTransformNode*>(root->firstChild());
    liveT = static_cast<QSGTransformNode*>(content->nextSibling());
    overlayT = static_cast<QSGTransformNode*>(liveT->nextSibling());
    rulerT = static_cast<QSGTransformNode*>(overlayT->nextSibling());
    ring = static_cast<QSGGeometryNode*>(rulerT->nextSibling());
    laser = static_cast<QSGGeometryNode*>(ring->nextSibling());
    liveClip = static_cast<QSGClipNode*>(liveT->firstChild());
    liveGlow = static_cast<QSGGeometryNode*>(liveClip->firstChild());
    live = static_cast<QSGGeometryNode*>(liveGlow->nextSibling());
  }

  // Nodes of items that went away.
  for (auto& it : m_graveyard) {
    if (it->node) {
      if (QSGNode* parent = it->node->parent()) parent->removeChildNode(it->node);
      delete it->node;
      it->node = nullptr;
      it->texture = nullptr;
    }
  }
  m_graveyard.clear();

  const QPointF o = origin();
  QMatrix4x4 m;
  m.translate(float(o.x()), float(o.y()));
  m.scale(float(m_zoom));
  content->setMatrix(m);
  content->markDirty(QSGNode::DirtyMatrix);

  if (m_rebuildAll || m_nodes.size() != m_pages.size()) {
    clearChildren(content);
    // Items' nodes were children of pages: forget them.
    for (Page& p : m_pages)
      for (auto& it : p.items) {
        it->node = nullptr;
        it->texture = nullptr;
        if (!it->mesh) it->imageDirty = !it->image.isNull();
      }
    m_nodes.assign(m_pages.size(), {});
    for (size_t i = 0; i < m_pages.size(); ++i) {
      Page& pg = m_pages[i];
      auto* t = new QSGTransformNode;
      QMatrix4x4 pm;
      pm.translate((m_maxPageW - pg.w) / 2, pg.top);
      t->setMatrix(pm);
      content->appendChildNode(t);
      t->appendChildNode(new QSGSimpleRectNode(QRectF(1, 2, pg.w, pg.h), QColor(0, 0, 0, 22)));
      auto* paper = win->createImageNode();
      paper->setOwnsTexture(true);
      paper->setRect(QRectF(0, 0, pg.w, pg.h));
      paper->setFiltering(QSGTexture::Linear);
      t->appendChildNode(paper);
      auto* detail = new QSGNode;  // holds the sharper part's image node, when there is one
      t->appendChildNode(detail);
      auto* items = new QSGNode;
      t->appendChildNode(items);
      // What's on the page shows only on the paper: anything past its edge
      // (a shape, text, ink from older versions) is covered by the desk.
      auto* mask = new QSGNode;
      t->appendChildNode(mask);
      m_nodes[i] = {0, t, paper, detail, nullptr, nullptr, items, mask};
      pg.paperDirty = !pg.paperImg.isNull();
      pg.detailDirty = true;
      // Until the picture is ready: the paper colour.
      if (pg.paperImg.isNull()) {
        QImage fill(1, 1, QImage::Format_ARGB32_Premultiplied);
        fill.fill(Canvas::parseColor(pg.paper));
        paper->setTexture(win->createTextureFromImage(fill));
      }
    }
    m_rebuildAll = false;
  }

  for (size_t i = 0; i < m_pages.size(); ++i) {
    Page& pg = m_pages[i];
    PageNodes& n = m_nodes[i];
    auto* paper = static_cast<QSGImageNode*>(n.paper);
    {
      // The desk around the page (the shadow's edge left showing); the first
      // and last pages' strips reach far above and below.
      const double far = 100000, gap = kPageGap / 2 + 1;
      const double top = i == 0 ? far : gap, bottom = i + 1 == m_pages.size() ? far : gap;
      const QRectF rects[] = {{-far, -top, pg.w + 2 * far, top}, {-far, pg.h + 2.5, pg.w + 2 * far, bottom}, {-far, -top, far, pg.h + top + bottom}, {pg.w + 1.5, -top, far, pg.h + top + bottom}};
      QSGNode* mask = n.mask;
      if (!mask->firstChild())
        for (const QRectF& r : rects) mask->appendChildNode(new QSGSimpleRectNode(r, m_deskColor));
      int k = 0;
      for (QSGNode* c = mask->firstChild(); c; c = c->nextSibling(), ++k) {
        auto* rn = static_cast<QSGSimpleRectNode*>(c);
        if (rn->rect() != rects[k]) rn->setRect(rects[k]);
        if (rn->color() != m_deskColor) rn->setColor(m_deskColor);
      }
    }
    QSGNode* detail = n.detail;
    if (pg.paperDirty && !pg.paperImg.isNull()) {
      paper->setTexture(win->createTextureFromImage(pg.paperImg));
      paper->setRect(QRectF(0, 0, pg.w, pg.h));
      paper->markDirty(QSGNode::DirtyMaterial);
      pg.paperDirty = false;
    }
    if (pg.detailDirty) {
      clearChildren(detail);
      if (!pg.detailImg.isNull()) {
        auto* img = win->createImageNode();
        img->setOwnsTexture(true);
        img->setFiltering(QSGTexture::Linear);
        img->setTexture(win->createTextureFromImage(pg.detailImg));
        img->setRect(pg.detailRect);
        detail->appendChildNode(img);
      }
      pg.detailDirty = false;
    }
    if (n.version == pg.version) continue;
    // Items in z order; unchanged ones keep their GPU data.
    QSGNode* container = n.items;
    while (QSGNode* c = container->firstChild()) container->removeChildNode(c);
    for (auto& it : pg.items) {
      if (!it->mesh && it->image.isNull()) continue;  // its picture comes once drawn
      if (!it->node) {
        auto* t = new QSGTransformNode;
        if (it->mesh) {
          if (it->tris && !it->tris->tris.empty()) t->appendChildNode(meshNode(it->tris->tris, it->color));
        } else {
          auto* img = win->createImageNode();
          img->setOwnsTexture(true);
          img->setFiltering(QSGTexture::Linear);
          img->setRect(it->rect);
          t->appendChildNode(img);
        }
        it->node = t;
      }
      if (!it->mesh && it->imageDirty && !it->image.isNull()) {
        auto* img = static_cast<QSGImageNode*>(it->node->firstChild());
        img->setTexture(win->createTextureFromImage(it->image));
        img->setRect(it->rect);
        img->markDirty(QSGNode::DirtyMaterial | QSGNode::DirtyGeometry);
        it->imageDirty = false;
      }
      container->appendChildNode(it->node);
    }
    n.version = pg.version;
    m_liveTransformDirty = true;
  }

  // Selected items following the pointer.
  if (m_liveTransformDirty) {
    m_liveTransformDirty = false;
    const int pi = activePage();
    if (pi >= 0 && pi < int(m_pages.size()))
      for (auto& it : m_pages[pi].items) {
        if (!it->node) continue;
        auto* t = static_cast<QSGTransformNode*>(it->node);
        const auto found = m_liveTransform.constFind(it->id);
        const QMatrix4x4 mm = found == m_liveTransform.constEnd() ? QMatrix4x4() : QMatrix4x4(*found);
        if (t->matrix() != mm) {
          t->setMatrix(mm);
          t->markDirty(QSGNode::DirtyMatrix);
        }
      }
  }

  // The stroke being written.
  if (m_liveChanged) {
    m_liveChanged = false;
    if (m_g.kind == Gesture::Draw && m_g.mesh && !m_liveHidden) {
      setMesh(live, m_g.mesh->tris, inkColor(m_g.color, m_g.pen));
      QColor glow = m_g.color;
      glow.setAlphaF(0.16f);
      setMesh(liveGlow, m_g.mesh->glow, glow);
    } else {
      setMesh(live, {}, Qt::black);
      setMesh(liveGlow, {}, Qt::transparent);
    }
  }
  if (m_g.kind == Gesture::Draw && m_g.page >= 0 && m_g.page < int(m_pages.size())) {
    const Page& pg = m_pages[m_g.page];
    QMatrix4x4 lm = m;
    lm.translate((m_maxPageW - pg.w) / 2, pg.top);
    liveT->setMatrix(lm);
    liveT->markDirty(QSGNode::DirtyMatrix);
    const QRectF clip(0, 0, pg.w, pg.h);
    if (liveClip->clipRect() != clip) {
      liveClip->setClipRect(clip);
      QSGGeometry::updateRectGeometry(liveClip->geometry(), clip);
      liveClip->markDirty(QSGNode::DirtyGeometry);
    }
  }

  // The overlay (selection, lasso, previews), in its page's place.
  clearChildren(overlayT);
  if (!m_overlayImg.isNull() && m_overlayPage >= 0 && m_overlayPage < int(m_pages.size())) {
    const Page& pg = m_pages[m_overlayPage];
    QMatrix4x4 om = m;
    om.translate((m_maxPageW - pg.w) / 2, pg.top);
    overlayT->setMatrix(om);
    overlayT->markDirty(QSGNode::DirtyMatrix);
    auto* img = win->createImageNode();
    img->setOwnsTexture(true);
    img->setTexture(win->createTextureFromImage(m_overlayImg));
    img->setRect(m_overlayRect);
    img->setFiltering(QSGTexture::Linear);
    overlayT->appendChildNode(img);
  }

  clearChildren(rulerT);  // (instruments are drawn in QML)

  // (The eraser's ring is drawn in QML over the canvas: EraserCursor.)
  setMesh(ring, {}, Qt::transparent);

  // (The laser is drawn by LaserTrail items over the canvas.)
  clearChildren(laser);
  setMesh(laser, {}, Qt::transparent);

  if (profile) qInfo("scene update %.2f ms", timer.nsecsElapsed() / 1e6);
  return root;
}
