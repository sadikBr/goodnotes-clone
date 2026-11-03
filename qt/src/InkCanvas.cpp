#include "InkCanvas.h"

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
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>
#include <QTabletEvent>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cmath>

#include "Core.h"
#include "Js.h"
#include "JsCanvas.h"
#include "Pdf.h"

namespace {
constexpr float kPageGap = 28;     // page units between pages
constexpr qreal kPad = 32;         // screen pixels beside the pages
constexpr qreal kPadTop = 74;      // above them (room for the tool drawer)
constexpr qreal kPadBottom = 84;   // below them (room for the page controls)
constexpr qreal kMinZoom = 0.25, kMaxZoom = 6;
constexpr float kLazyPen = 1.1f;   // screen pixels the ink may trail the pen
constexpr int kMaxTexture = 4096;  // pixels per side of one picture
constexpr double kLaserHold = 900, kLaserFade = 300, kLaserCoreFade = 480;

double nowMs() { return Js::clockMs(); }

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

QColor inkColor(const QColor& c, ink::Pen pen) {
  if (pen != ink::Pen::Highlighter) return c;
  QColor h = c;
  h.setAlphaF(0.35f);
  return h;
}

void clearChildren(QSGNode* n) {
  while (QSGNode* c = n->firstChild()) {
    n->removeChildNode(c);
    delete c;
  }
}

/// Picture scales in steps of √2, so small zoom changes reuse what's drawn.
double bucket(double s) { return std::clamp(std::pow(2.0, std::ceil(std::log2(std::max(s, 0.05)) * 2) / 2), 0.25, 16.0); }

QJSValueList jsArgs(std::initializer_list<QJSValue> l) { return QJSValueList(l); }
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
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(600);
  connect(&m_saveTimer, &QTimer::timeout, this, &InkCanvas::flush);
  // Imported PDF pages get their thumbnails in the background, one at a time.
  m_scrollGlide.setInterval(8);
  connect(&m_scrollGlide, &QTimer::timeout, this, [this] {
    const QPointF before = m_scroll;
    m_scroll += (m_glideTarget - m_scroll) * 0.28;
    if (QLineF(m_scroll, m_glideTarget).length() < 0.75) m_scroll = m_glideTarget;
    clampScroll();
    if (m_scroll == before || m_scroll == m_glideTarget) m_scrollGlide.stop();
    m_overlayDirty = true;
    emit viewChanged();
    refresh();
  });
  m_thumbFill.setSingleShot(true);
  m_thumbFill.setInterval(250);
  connect(&m_thumbFill, &QTimer::timeout, this, [this] {
    for (int i = 0; i < int(m_pages.size()); ++i) {
      const QJsonObject& m = m_pages[i].meta;
      if (m["backgroundAsset"].toString().isEmpty() || !m["thumbnail"].toString().isEmpty()) continue;
      storeThumbnail(i);
      emit pagesChanged();
      if (!m_pages[i].meta["thumbnail"].toString().isEmpty()) m_thumbFill.start();
      return;
    }
  });
  m_settle.setSingleShot(true);
  m_settle.setInterval(160);
  connect(&m_settle, &QTimer::timeout, this, [this] {
    renderPictures();
    update();
  });
  m_timers.setInterval(30);
  connect(&m_timers, &QTimer::timeout, this, [this] {
    Js& js = Js::instance();
    QJSValue g = js.engine().globalObject();
    g.property("__runTimers").call();
    if (!g.property("__timersPending").call().toBool()) m_timers.stop();
    afterCore();
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

QJSValue InkCanvas::core(const QString& method, const QJSValueList& args) {
  if (m_core.isUndefined() || m_core.isNull()) return {};
  QJSValue f = m_core.property(method);
  if (!f.isCallable()) return {};
  static const bool trace = qEnvironmentVariableIsSet("INKLEAF_TRACE");
  QElapsedTimer t;
  if (trace) t.start();
  QJSValue r = f.callWithInstance(m_core, args);
  if (trace && t.nsecsElapsed() > 3e6) qInfo("core.%s %.1f ms", qPrintable(method), t.nsecsElapsed() / 1e6);
  if (r.isError()) qWarning().noquote() << "editor:" << method << r.toString() << r.property("stack").toString();
  return r;
}

QJSValue InkCanvas::coreProp(const QString& name) { return m_core.property(name); }

void InkCanvas::pushToolState() {
  if (m_core.isUndefined()) return;
  Js& js = Js::instance();
  m_core.setProperty("tool", m_tool);
  m_core.setProperty("colors", js.toJs(QJsonObject::fromVariantMap(m_colors)));
  m_core.setProperty("widths", js.toJs(QJsonObject::fromVariantMap(m_widths)));
  m_core.setProperty("penStyle", m_penStyleName);
  m_core.setProperty("streamline", m_smoothing);
  m_core.setProperty("shapeKind", m_shapeKind);
  m_core.setProperty("shapeFill", m_shapeFill);
  m_core.setProperty("eraserMode", m_eraserMode);
  m_core.setProperty("textFont", m_textFont);
  m_core.setProperty("zoom", m_zoom);
  m_core.setProperty("viewWidth", width());
}

void InkCanvas::afterCore() {
  if (m_core.isUndefined()) return;
  const double rev = coreProp("rev").toNumber();
  if (rev != m_coreRev) {
    m_coreRev = rev;
    for (int i = 0; i < int(m_pages.size()); ++i)
      if (m_pages[i].synced) syncPage(i);
    emit historyChanged();
    if (core("dirty").toBool()) m_saveTimer.start();
  }
  const double orev = coreProp("overlayRev").toNumber();
  if (orev != m_overlayRev) {
    m_overlayRev = orev;
    m_overlayDirty = true;
  }
  const double irev = coreProp("instrumentRev").toNumber();
  if (irev != m_instrumentRev) {
    m_instrumentRev = irev;
    m_instrument = core("instrument").toVariant(QJSValue::ConvertJSObjects).toMap();
    emit instrumentChanged();
  }
  const double srev = coreProp("selectionRev").toNumber();
  if (srev != m_selectionRev) {
    m_selectionRev = srev;
    syncSelection();
  }
  // Selected items following the pointer.
  QJSValue lt = core("liveTransform");
  QHash<QString, QTransform> next;
  if (lt.isObject()) {
    const QJSValue m = lt.property("m");
    const QTransform t(m.property("a").toNumber(), m.property("b").toNumber(), m.property("c").toNumber(), m.property("d").toNumber(), m.property("e").toNumber(),
                       m.property("f").toNumber());
    const QJSValue ids = lt.property("ids");
    const int n = ids.property("length").toInt();
    for (int k = 0; k < n; ++k) next.insert(ids.property(k).toString(), t);
  }
  if (next != m_liveTransform) {
    m_liveTransform = next;
    m_liveTransformDirty = true;
  }
  const bool hidden = core("liveHidden").toBool();
  if (hidden != m_liveHidden) {
    m_liveHidden = hidden;
    m_liveChanged = true;
  }
  if (Js::instance().engine().globalObject().property("__timersPending").call().toBool() && !m_timers.isActive()) m_timers.start();
  refresh();
}

void InkCanvas::syncSelection() {
  m_selection = core("selectionInfo").toVariant(QJSValue::ConvertJSObjects).toMap();
  emit selectionChanged();
}

void InkCanvas::syncEditing() {
  QJSValue e = coreProp("editing");
  QVariantMap next;
  if (e.isObject()) {
    next = e.toVariant(QJSValue::ConvertJSObjects).toMap();
    next["page"] = int(coreProp("active").toInt());
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
  Js& js = Js::instance();
  // "id rev[ h]" per element, in z order.
  const QString keys = core("sceneKeys", jsArgs({i})).toString();
  const QStringList lines = keys.split('\n', Qt::SkipEmptyParts);
  struct Want {
    QString id;
    double rev;
  };
  std::vector<Want> want;
  want.reserve(size_t(lines.size()));
  QStringList fetch;
  for (const QString& line : lines) {
    const QStringList f = line.split(' ');
    if (f.size() < 2) continue;
    if (f.size() > 2) continue;  // hidden (being edited, or part of a gesture)
    const double rev = f[1].toDouble();
    want.push_back({f[0], rev});
    auto found = pg.byId.find(f[0]);
    if (found == pg.byId.end() || found->second->rev != rev) fetch << f[0];
  }
  // The changed elements, with stroke points packed.
  std::unordered_map<QString, std::shared_ptr<Item>> fresh;
  if (!fetch.isEmpty()) {
    QJSValue ids = js.engine().newArray(uint(fetch.size()));
    for (int k = 0; k < fetch.size(); ++k) ids.setProperty(quint32(k), fetch[k]);
    QJSValue packed = core("packElements", {i, ids});
    const QJsonArray meta = QJsonDocument::fromJson(packed.property("meta").toString().toUtf8()).array();
    const QByteArray pts = packed.property("pts").toVariant().toByteArray();
    const float* f = reinterpret_cast<const float*>(pts.constData());
    const size_t nf = size_t(pts.size()) / sizeof(float);
    size_t o = 0;
    for (const auto& v : meta) {
      const QJsonObject m = v.toObject();
      auto it = std::make_shared<Item>();
      it->id = m["id"].toString();
      it->rev = m["rev"].toDouble();
      const QString kind = m["kind"].toString();
      const QJsonObject b = m["b"].toObject();
      const QRectF box(b["x"].toDouble(), b["y"].toDouble(), b["w"].toDouble(), b["h"].toDouble());
      if (kind == "stroke") {
        const int n = m["n"].toInt();
        std::vector<ink::Point> p;
        p.reserve(size_t(n));
        for (int q = 0; q < n && o + 2 < nf; ++q, o += 3) p.push_back({f[o], f[o + 1], f[o + 2]});
        const QString style = m["style"].toString();
        const bool hl = m["tool"].toString() == "highlighter";
        if (hl || (style != "pencil" && style != "calligraphy")) {
          // Pen and highlighter ink: a mesh, as when it was written.
          it->mesh = true;
          const ink::Pen pen = ink::penFromName(style, hl);
          it->tris = std::make_shared<ink::Mesh>();
          ink::strokeMesh(p, pen, float(m["width"].toDouble(3)), m["smooth"].isUndefined() || m["smooth"].isNull() ? ink::kSmoothNormal : float(m["smooth"].toDouble()), *it->tris);
          it->color = inkColor(JsCanvas::parseColor(m["color"].toString("#111827")), pen);
        } else {
          // Textured nibs: drawn by the web code.
          QJsonObject d = m;
          d.remove("n");
          d.remove("b");
          d.remove("rev");
          QJsonArray arr;
          for (const ink::Point& q : p) arr.append(QJsonArray{q.x, q.y, q.p});
          d["points"] = arr;
          if (d["smooth"].isNull()) d.remove("smooth");
          it->data = d;
          it->rect = box.adjusted(-6, -6, 6, 6);
        }
      } else {
        it->data = m["el"].toObject();
        const double pad = kind == "sticky" ? 18 : kind == "shape" ? 12 : 6;
        it->rect = box.adjusted(-pad, -pad, pad, pad);
      }
      fresh.emplace(it->id, it);
    }
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
  if (trace && timer.nsecsElapsed() > 3e6) qInfo("syncPage %d %.1f ms (%d fetched)", i, timer.nsecsElapsed() / 1e6, int(fetch.size()));
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
  m_core = QJSValue();
  m_coreRev = m_overlayRev = m_selectionRev = -1;
  QJsonArray pages;
  if (!m_notebookId.isEmpty()) {
    QString err;
    pages = InkleafCore::instance()->callJson("pages", {{"notebookId", m_notebookId}}, &err).toArray();
    if (!err.isEmpty()) emit message(err, true);
    Js& js = Js::instance();
    m_core = js.call("createEditor", {js.toJs(pages)});
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
    core("forgetAll");
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
  core("setPages", {Js::instance().toJs(pages)});
  applyPages(pages);
  for (int i = 0; i < int(m_pages.size()); ++i) m_pages[i].synced = false;
  m_coreRev = -1;
  afterCore();
}

QVariant InkCanvas::daftarQuery(const QString& fn, const QVariantList& args) {
  if (m_core.isUndefined()) return {};
  Js& js = Js::instance();
  QJSValueList list{m_core};
  for (const QVariant& a : args) list << js.engine().toScriptValue(a);
  return js.awaitCall("daftarQt." + fn, list).toVariant(QJSValue::ConvertJSObjects);
}

QVariantMap InkCanvas::daftar(const QString& fn, const QVariantList& args) {
  if (m_core.isUndefined()) return {};
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
  if (m_core.isUndefined()) return;
  if (!m_editing.isEmpty()) {
    // Typing continues: save the rest when the box closes.
  }
  QJSValue dirty = core("takeDirty");
  const int n = dirty.property("length").toInt();
  if (n == 0) return;
  for (int k = 0; k < n; ++k) {
    const int i = dirty.property(k).toInt();
    if (i < 0 || i >= int(m_pages.size())) continue;
    // The stored form, written here (much faster than in the script engine):
    // [{ id, kind, z, data: element }] with each stroke's points put back.
    QJSValue packed = core("packForSave", {i});
    const QJsonArray meta = QJsonDocument::fromJson(packed.property("meta").toString().toUtf8()).array();
    const QByteArray pts = packed.property("pts").toVariant().toByteArray();
    const float* f = reinterpret_cast<const float*>(pts.constData());
    const size_t nf = size_t(pts.size()) / sizeof(float);
    size_t o = 0;
    QByteArray out;
    out.reserve(pts.size() * 3 + 4096);
    out += "{\"pageId\":";
    out += QJsonDocument(QJsonArray{m_pages[i].id}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
    out += ",\"elements\":[";
    bool first = true;
    auto num = [&](float v, int decimals) {
      // Short decimals, like the web app's stored points.
      QByteArray t = QByteArray::number(double(v), 'f', decimals);
      if (t.contains('.')) {
        while (t.endsWith('0')) t.chop(1);
        if (t.endsWith('.')) t.chop(1);
      }
      if (t == "-0") t = "0";
      out += t;
    };
    for (const auto& v : meta) {
      QJsonObject el = v.toObject();
      const int np = el.take("__n").toInt(-1);
      if (!first) out += ',';
      first = false;
      out += "{\"id\":" + QJsonDocument(QJsonArray{el["id"]}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
      out += ",\"kind\":" + QJsonDocument(QJsonArray{el["kind"]}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
      out += ",\"z\":" + QByteArray::number(el["z"].toDouble());
      out += ",\"data\":";
      QByteArray data = QJsonDocument(el).toJson(QJsonDocument::Compact);
      if (np >= 0) {
        data.chop(1);  // the closing brace: the points go in before it
        out += data;
        out += data.endsWith('{') ? "\"points\":[" : ",\"points\":[";
        for (int q = 0; q < np && o + 2 < nf; ++q, o += 3) {
          if (q) out += ',';
          out += '[';
          num(f[o], 2);
          out += ',';
          num(f[o + 1], 2);
          out += ',';
          num(f[o + 2], 3);
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
      core("markDirty", {i});
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
  // Covers are drawn sharper: the library shows them full-size.
  const bool cover = p.tmpl.startsWith("cover");
  const double s = (cover ? 420.0 : 200.0) / p.w;
  QImage img(int(std::ceil(p.w * s)), int(std::ceil(p.h * s)), QImage::Format_RGB32);
  img.fill(JsCanvas::parseColor(p.paper));
  QPainter painter(&img);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
  painter.scale(s, s);
  if (p.paperImg.isNull()) p.paperImg = paperWithBackground(p, std::max(s, 0.5), {}), p.paperScale = std::max(s, 0.5), p.paperDirty = true;
  painter.drawImage(QRectF(0, 0, p.w, p.h), p.paperImg);
  for (const auto& it : p.items) {
    if (it->mesh) {
      if (!it->tris || it->tris->tris.empty()) continue;
      // The mesh's triangles as one path (overlaps fill once, as on screen per stroke).
      QPainterPath path;
      path.setFillRule(Qt::WindingFill);
      const auto& t = it->tris->tris;
      for (size_t k = 0; k + 2 < t.size(); k += 3) {
        path.moveTo(t[k]);
        path.lineTo(t[k + 1]);
        path.lineTo(t[k + 2]);
        path.closeSubpath();
      }
      painter.fillPath(path, it->color);
    } else {
      if (it->image.isNull()) it->image = Js::instance().elements(QJsonArray{it->data}, std::max(s * 2, 0.5), it->rect), it->scale = std::max(s * 2, 0.5), it->imageDirty = true;
      painter.drawImage(it->rect, it->image);
    }
  }
  painter.end();
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "JPEG", 82);
  const QString url = "data:image/jpeg;base64," + QString::fromLatin1(bytes.toBase64());
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
      core("load", {i});
      m_core.property("pages").property(i).setProperty("dirty", true);
    }
  flush();
}

QString InkCanvas::pagePreview(int index, qreal scale) {
  if (index < 0 || index >= int(m_pages.size())) return {};
  Js& js = Js::instance();
  const Page& p = m_pages[index];
  QJSValue els = core("scene", jsArgs({index}));
  QJSValue canvas = js.call("renderPage", {p.tmpl, p.paper, p.w, p.h, els, scale, coreProp("images")});
  QObject* o = canvas.toQObject();
  auto* c = qobject_cast<JsCanvas*>(o);
  return c ? c->toDataURL("image/png") : QString();
}

QString InkCanvas::exportPdf(const QString& path, const QVariantList& indexes, const QString& title) {
  const QString file = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
  if (indexes.isEmpty()) return "Nothing to export";
  finishEdit();
  flush();
  Js& js = Js::instance();
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
    QJSValue bg(QJSValue::NullValue);
    const QString asset = p.meta["backgroundAsset"].toString();
    if (!asset.isEmpty() && !p.meta["backgroundPage"].isNull()) {
      const int bp = p.meta["backgroundPage"].toInt();
      const QImage img = PdfDocs::instance().render(asset, bp, 3 * p.w / std::max(1.0, PdfDocs::instance().pageSize(asset, bp).width()));
      if (!img.isNull()) bg = js.engine().newQObject(new JsImage(img.convertToFormat(QImage::Format_ARGB32_Premultiplied)));
    }
    auto* canvas = new JsCanvas(&painter, int(std::ceil(p.w)), int(std::ceil(p.h)));
    QJSValue ctx = js.engine().newQObject(canvas);
    js.call("paintPage", {ctx, p.tmpl, p.paper, p.w, p.h, core("scene", jsArgs({i})), coreProp("images"), bg});
    delete canvas;  // restores the painter for the next page
  }
  painter.end();
  return {};
}

QVariantMap InkCanvas::exportPage(int index, qreal scale, bool jpeg) {
  if (index < 0 || index >= int(m_pages.size())) return {};
  Js& js = Js::instance();
  const Page& p = m_pages[index];
  QJSValue els = core("scene", jsArgs({index}));
  // An imported PDF page as the background.
  QJSValue bg(QJSValue::NullValue);
  const QString asset = p.meta["backgroundAsset"].toString();
  if (!asset.isEmpty() && !p.meta["backgroundPage"].isNull()) {
    const QImage img = PdfDocs::instance().render(asset, p.meta["backgroundPage"].toInt(), scale * p.w / std::max(1.0, PdfDocs::instance().pageSize(asset, p.meta["backgroundPage"].toInt()).width()));
    if (!img.isNull()) bg = js.engine().newQObject(new JsImage(img.convertToFormat(QImage::Format_ARGB32_Premultiplied)));
  }
  QJSValue canvas = js.call("renderPage", {p.tmpl, p.paper, p.w, p.h, els, scale, coreProp("images"), bg});
  auto* c = qobject_cast<JsCanvas*>(canvas.toQObject());
  if (!c) return {};
  return {{"dataUrl", c->toDataURL(jpeg ? "image/jpeg" : "image/png", 0.9)}, {"widthPx", c->width()}, {"heightPx", c->height()}, {"pageWidth", p.w}, {"pageHeight", p.h}};
}

// --- tool state -------------------------------------------------------------------------

void InkCanvas::setTool(const QString& t) {
  if (t == m_tool) return;
  m_tool = t;
  if (!m_core.isUndefined()) {
    core("finishTextEdit");
    if (t != "lasso") core("clearSelection");
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
  if (m_core.isUndefined()) return;
  const bool shown = m_instrument.value("visible").toBool();
  if (kind.isEmpty() || (shown && m_instrument.value("kind").toString() == kind)) {
    core("hideInstrument");
  } else {
    // In the middle of the view, on the page in view.
    const int pi = currentPage();
    core("activate", {pi});
    QPointF c = toPageOf(pi, QPointF(width() / 2, height() / 2));
    if (pi < int(m_pages.size())) c = QPointF(std::clamp<qreal>(c.x(), 0, m_pages[pi].w), std::clamp<qreal>(c.y(), 0, m_pages[pi].h));
    if (shown) core("hideInstrument");
    core("showInstrument", {kind, c.x(), c.y()});
  }
  afterCore();
}

void InkCanvas::placeInstrument(qreal cx, qreal cy, qreal angle) {
  core("placeInstrument", {cx, cy, angle});
  afterCore();
}

void InkCanvas::setInstrumentArm(int arm, qreal degrees) {
  core("setArm", {arm, degrees});
  afterCore();
}

bool InkCanvas::instrumentContains(qreal x, qreal y) { return core("instrumentContains", {x, y}).toBool(); }

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

QColor InkCanvas::laserColor() const { return JsCanvas::parseColor(m_colors.value("laser", "#ff1f1f").toString()); }

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

bool InkCanvas::canUndo() const { return !m_core.isUndefined() && m_core.property("undoStack").property("length").toInt() > 0; }
bool InkCanvas::canRedo() const { return !m_core.isUndefined() && m_core.property("redoStack").property("length").toInt() > 0; }

void InkCanvas::undo() {
  core("undo");
  syncEditing();
  afterCore();
}
void InkCanvas::redo() {
  core("redo");
  syncEditing();
  afterCore();
}
void InkCanvas::deleteSelection() {
  core("deleteSelection");
  afterCore();
}
void InkCanvas::duplicateSelection() {
  core("duplicateSelection");
  afterCore();
}
int InkCanvas::copySelection() { return core("copySelection").toInt(); }
int InkCanvas::cutSelection() {
  const int n = core("cutSelection").toInt();
  afterCore();
  return n;
}
int InkCanvas::paste() {
  const int n = core("pasteClipboard").toInt();
  if (n) setTool("lasso");
  afterCore();
  return n;
}
void InkCanvas::selectAll() {
  setTool("lasso");
  core("selectAll");
  afterCore();
}
void InkCanvas::clearSelection() {
  core("clearSelection");
  afterCore();
}
void InkCanvas::recolorSelection(const QString& color) {
  core("recolorSelection", {color});
  afterCore();
}
void InkCanvas::styleSelection(const QVariantMap& change) {
  core("styleSelectedText", {Js::instance().toJs(QJsonObject::fromVariantMap(change))});
  afterCore();
}
void InkCanvas::changeShape(const QString& change, qreal value) {
  core("updateSelectedShape", {change, value});
  afterCore();
}

void InkCanvas::applyFontToInk(const QString& font) {
  Js& js = Js::instance();
  QJSValue ink = core("selectedInk");
  const int n = ink.property("length").toInt();
  if (!n) return;
  // Recognise the handwriting (the Rust core), then place it as text where it was.
  QJsonArray strokes;
  QJsonArray ids;
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  QString color = "#111827";
  for (int k = 0; k < n; ++k) {
    const QJsonObject el = js.toJson(ink.property(k)).toObject();
    ids.append(el["id"]);
    color = el["color"].toString(color);
    QJsonArray pts;
    for (const auto& p : el["points"].toArray()) {
      const QJsonArray a = p.toArray();
      pts.append(QJsonArray{a.at(0), a.at(1)});
      x0 = std::min(x0, a.at(0).toDouble()), x1 = std::max(x1, a.at(0).toDouble());
      y0 = std::min(y0, a.at(1).toDouble()), y1 = std::max(y1, a.at(1).toDouble());
    }
    strokes.append(pts);
  }
  QString err;
  const QJsonObject r = InkleafCore::instance()->callJson("recognizeHandwriting", {{"strokes", strokes}}, &err).toObject();
  const QString text = r["text"].toString().trimmed();
  if (!err.isEmpty() || text.isEmpty()) return emit message(err.isEmpty() ? "Couldn't read that handwriting" : err, true);
  // Text the height of the writing (one line per recognised line).
  const int lines = std::max<int>(1, int(text.count('\n')) + 1);
  const double size = std::clamp((y1 - y0) / lines / 1.15, 10.0, 96.0);
  const QJsonObject el{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"z", 0}, {"kind", "text"}, {"x", x0}, {"y", y0}, {"w", std::max(60.0, x1 - x0 + size)},
                       {"text", text}, {"fontSize", std::round(size)}, {"color", color}, {"font", font}};
  core("replaceElements", {js.toJs(ids), js.toJs(QJsonArray{el})});
  afterCore();
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
  core("activate", {pi});
  const QPointF c = toPageOf(pi, QPointF(width() / 2, height() / 2));
  setTool("lasso");
  core("insertImage", {src, img.width(), img.height(), c.x(), c.y()});
  afterCore();
}

void InkCanvas::updateEdit(const QString& text) {
  core("updateEdit", {text});
  m_editing["text"] = text;
}

QVariantMap InkCanvas::continueList(const QString& text, int caret) { return core("continueList", {text, caret}).toVariant(QJSValue::ConvertJSObjects).toMap(); }

void InkCanvas::editFormat(const QVariantMap& change) {
  core("applyFormat", {Js::instance().toJs(QJsonObject::fromVariantMap(change))});
  if (change.contains("font")) m_textFont = change["font"].toString();
  syncEditing();
  emit toolStateChanged();
}

void InkCanvas::finishEdit() {
  core("finishTextEdit");
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
  if (!m_core.isUndefined()) m_core.setProperty("viewWidth", n.width());
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

int InkCanvas::currentPage() const {
  int i = -1;
  toPage(QPointF(width() / 2, height() / 2), &i);
  return std::max(0, i);
}

int InkCanvas::activePage() const { return m_core.isUndefined() ? 0 : m_core.property("active").toInt(); }

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
}

void InkCanvas::updatePolish() {
  // GUI thread, before the frame: elements and pictures for pages coming into view.
  if (m_core.isUndefined()) return;
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
  renderPictures(m_settle.isActive());
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
    const int pi = coreProp("active").toInt();
    QJSValue b = core("overlayBounds");
    if (b.isObject() && pi >= 0 && pi < int(m_pages.size())) {
      const Page& pg = m_pages[pi];
      // Only what's on screen.
      const QRectF view(toPageOf(pi, QPointF(0, 0)), toPageOf(pi, QPointF(width(), height())));
      const QRectF r = QRectF(b.property("x").toNumber(), b.property("y").toNumber(), b.property("w").toNumber(), b.property("h").toNumber()).intersected(view.adjusted(-20, -20, 20, 20));
      Q_UNUSED(pg);
      if (!r.isEmpty()) {
        const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
        const double s = m_zoom * dpr;
        Js& js = Js::instance();
        auto* canvas = new JsCanvas(int(std::ceil(r.width() * s)), int(std::ceil(r.height() * s)));
        QJSValue ctx = js.engine().newQObject(canvas);
        canvas->setTransform(s, 0, 0, s, -r.x() * s, -r.y() * s);
        core("drawOverlay", {ctx, pi});
        m_overlayImg = canvas->image();
        m_overlayRect = r;
        m_overlayPage = pi;
      }
    }
    update();
  }
}

QImage InkCanvas::paperWithBackground(const Page& p, double scale, QRectF region) {
  QImage img = Js::instance().paper(p.tmpl, p.paper, p.w, p.h, scale, region);
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
  Js& js = Js::instance();
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
      it->image = js.elements(QJsonArray{it->data}, want, it->rect);
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
  if (m_core.isUndefined()) return;
  m_scrollGlide.stop();
  m_g = Gesture();
  if (m_dismissOnPress) {
    // Closes the drawer and does nothing else (the rest of this press is ignored).
    emit dismissed();
    return;
  }
  const QString tool = toolOverride.isEmpty() ? m_tool : toolOverride;
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
  const int pi = pageAt(s);
  if (pi < 0) {
    if (m_tool == "lasso") core("clearSelection");
    core("finishTextEdit");
    syncEditing();
    afterCore();
    return;
  }
  if (!m_pages[pi].synced) syncPage(pi);
  const QPointF p = toPageOf(pi, s);
  m_g.page = pi;
  const QString r = core("down", {pi, p.x(), p.y(), pressure, nowMs(), tool, bool(mods & Qt::ShiftModifier)}).toString();
  m_g.kind = Gesture::Core;
  if (r == "draw") {
    m_g.kind = Gesture::Draw;
    const bool hl = tool == "highlighter";
    const bool shape = tool == "shape";
    m_g.pen = hl ? ink::Pen::Highlighter : shape ? ink::Pen::Ballpoint : ink::penFromName(m_penStyleName, false);
    m_g.color = JsCanvas::parseColor(m_colors.value(tool).toString());
    m_g.width = float(m_widths.value(tool, 3).toDouble());
    const float pr = ink::speedSensitive(m_g.pen) ? m_g.speed.next(float(p.x()), float(p.y()), nowMs(), stylus ? pressure : -1.f) : (stylus ? pressure : 0.5f);
    m_g.pts.push_back({float(p.x()), float(p.y()), pr});
    m_g.mesh = std::make_shared<ink::Mesh>();
    ink::strokeMesh(m_g.pts, m_g.pen, m_g.width, shape ? 0.f : float(m_smoothing), *m_g.mesh);
    m_liveChanged = true;
  } else if (r == "edit") {
    m_g = Gesture();
    syncEditing();
  }
  if (tool == "eraser") m_hover = s;
  afterCore();
}

void InkCanvas::drag(QPointF s, float pressure, bool stylus, Qt::KeyboardModifiers mods) {
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
      core("move", {p.x(), p.y(), pressure, nowMs(), bool(mods & Qt::ShiftModifier), bool(mods & Qt::AltModifier)});
      if (m_tool == "eraser" || m_g.kind == Gesture::Core) m_hover = s;
      afterCore();
      return;
    }
    case Gesture::Draw: {
      QPointF p = toPageOf(m_g.page, s);
      if (m_liveHidden) {
        // A refined shape follows the pen (rotate, resize, move).
        core("move", {p.x(), p.y(), pressure, nowMs(), bool(mods & Qt::ShiftModifier), false});
        afterCore();
        return;
      }
      const ink::Point& last = m_g.pts.back();
      // Lazy pen: the ink trails the tip by at most about a screen pixel.
      const float lazy = float(kLazyPen / m_zoom);
      const float d = float(std::hypot(p.x() - last.x, p.y() - last.y));
      if (d <= lazy) return;
      p = QPointF(last.x + (p.x() - last.x) * (d - lazy) / d, last.y + (p.y() - last.y) * (d - lazy) / d);
      if (std::hypot(p.x() - last.x, p.y() - last.y) < 0.4 / m_zoom) return;
      const float pr = ink::speedSensitive(m_g.pen) ? m_g.speed.next(float(p.x()), float(p.y()), nowMs(), stylus ? pressure : -1.f) : (stylus ? pressure : 0.5f);
      m_g.pts.push_back({float(p.x()), float(p.y()), pr});
      ink::strokeMesh(m_g.pts, m_g.pen, m_g.width, m_g.pen == ink::Pen::Ballpoint && m_tool == "shape" ? 0.f : float(m_smoothing), *m_g.mesh);
      m_liveChanged = true;
      core("move", {p.x(), p.y(), pr, nowMs(), bool(mods & Qt::ShiftModifier), false});
      afterCore();
      return;
    }
  }
}

void InkCanvas::release() {
  const Gesture::Kind k = m_g.kind;
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
  if (k == Gesture::Core || k == Gesture::Draw) core("up");
  m_g = Gesture();
  m_liveChanged = true;
  afterCore();
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
    if (m_g.touch && m_palmRejection) {
      core("cancel");
      m_g = Gesture();
    }
    press(local, curved(te->pressure()), true, override, false, te->modifiers());
    te->accept();
    return true;
  }
  if (type == QEvent::TabletMove) {
    if (m_g.kind == Gesture::None) {
      m_hover = local;
      if (m_tool == "eraser") update();
      if (m_tool == "laser") {
        m_laserDot = inside ? local : QPointF(-1, -1);
        emit laserChanged();
      }
      return false;
    }
    m_hover = local;
    drag(local, curved(te->pressure()), true, te->modifiers());
    te->accept();
    return true;
  }
  if (m_g.kind == Gesture::None) return false;
  release();
  te->accept();
  return true;
}

void InkCanvas::mousePressEvent(QMouseEvent* e) {
  if (e->device() && e->device()->type() == QInputDevice::DeviceType::Stylus) return e->ignore();
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
  if (e->device() && e->device()->type() == QInputDevice::DeviceType::Stylus) return e->ignore();
  m_hover = e->position();
  drag(e->position(), 0.5f, false, e->modifiers());
}

void InkCanvas::mouseReleaseEvent(QMouseEvent* e) {
  if (e->device() && e->device()->type() == QInputDevice::DeviceType::Stylus) return e->ignore();
  release();
}

void InkCanvas::hoverMoveEvent(QHoverEvent* e) {
  m_hover = e->position();
  if (m_tool == "laser") {
    m_laserDot = e->position();
    emit laserChanged();
  }
  if (m_tool == "eraser") update();
  if (m_tool == "lasso" && !m_core.isUndefined()) {
    int pi = -1;
    const QPointF p = toPage(e->position(), &pi);
    if (pi == activePage()) {
      const QString c = core("cursorAt", {p.x(), p.y()}).toString();
      setCursor(c == "move" ? Qt::SizeAllCursor : c == "ns" ? Qt::SizeVerCursor : c == "ew" ? Qt::SizeHorCursor : c == "nwse" ? Qt::SizeFDiagCursor : c == "nesw" ? Qt::SizeBDiagCursor
                : c == "rotate" || c == "crosshair" ? Qt::CrossCursor : Qt::ArrowCursor);
    }
  }
}

void InkCanvas::hoverLeaveEvent(QHoverEvent*) {
  m_hover = QPointF(-1, -1);
  m_laserDot = QPointF(-1, -1);
  emit laserChanged();
  update();
}

void InkCanvas::wheelEvent(QWheelEvent* e) {
  if (e->modifiers() & Qt::ControlModifier) {
    zoomAt(m_zoom * std::exp(e->angleDelta().y() * 0.0025), e->position());
    return;
  }
  // Touchpads scroll by their pixels; a mouse wheel by a step per notch, as
  // in a browser (on Wayland a wheel also reports a few pixels per notch,
  // which would barely move the page).
  const QPoint px = e->pixelDelta();
  const bool touchpad = e->phase() != Qt::NoScrollPhase || (e->pointingDevice() && e->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad);
  // A notch moves a good step, and notches that come quickly (a flick of the
  // wheel) move further and further: two quick flicks pass a page.
  const double now = nowMs();
  if (!touchpad) {
    m_wheelBoost = now - m_lastWheel < 90 ? std::min(4.0, m_wheelBoost * 1.25) : 1.0;
    m_lastWheel = now;
  }
  const QPointF d = touchpad && !px.isNull() ? QPointF(px) : QPointF(e->angleDelta()) / 120.0 * 160.0 * m_wheelBoost;
  const QPointF step = (e->modifiers() & Qt::ShiftModifier) ? QPointF(d.y(), 0) : d;
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
  if (!m_scrollGlide.isActive()) m_glideTarget = m_scroll;
  m_glideTarget -= step;
  m_scrollGlide.start();
}

void InkCanvas::touchEvent(QTouchEvent* e) {
  const auto& pts = e->points();
  if (pts.size() >= 2) {
    // Two fingers: pinch to zoom and move (a second finger cancels a touch stroke).
    if (m_g.kind == Gesture::Draw || m_g.kind == Gesture::Core) {
      core("cancel");
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

// --- scene graph --------------------------------------------------------------------------

QSGNode* InkCanvas::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  static const bool profile = qEnvironmentVariableIsSet("INKLEAF_PROFILE");
  QElapsedTimer timer;
  if (profile) timer.start();
  QQuickWindow* win = window();
  // root → content transform → pages; live ink; overlay; eraser ring; laser (screen space).
  auto* root = old;
  QSGTransformNode *content, *liveT, *overlayT, *rulerT;
  QSGGeometryNode *live, *ring, *laser;
  if (!root) {
    root = new QSGNode;
    content = new QSGTransformNode;
    liveT = new QSGTransformNode;
    overlayT = new QSGTransformNode;
    rulerT = new QSGTransformNode;
    live = meshNode({}, Qt::black);
    ring = meshNode({}, QColor(100, 100, 100, 200));
    laser = meshNode({}, Qt::red);
    static_cast<QSGFlatColorMaterial*>(laser->material())->setFlag(QSGMaterial::Blending);
    liveT->appendChildNode(live);
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
    live = static_cast<QSGGeometryNode*>(liveT->firstChild());
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
      m_nodes[i] = {0, t, paper, detail, nullptr, nullptr, items};
      pg.paperDirty = !pg.paperImg.isNull();
      pg.detailDirty = true;
      // Until the picture is ready: the paper colour.
      if (pg.paperImg.isNull()) {
        QImage fill(1, 1, QImage::Format_ARGB32_Premultiplied);
        fill.fill(JsCanvas::parseColor(pg.paper));
        paper->setTexture(win->createTextureFromImage(fill));
      }
    }
    m_rebuildAll = false;
  }

  for (size_t i = 0; i < m_pages.size(); ++i) {
    Page& pg = m_pages[i];
    PageNodes& n = m_nodes[i];
    auto* paper = static_cast<QSGImageNode*>(n.paper);
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
    if (m_g.kind == Gesture::Draw && m_g.mesh && !m_liveHidden) setMesh(live, m_g.mesh->tris, inkColor(m_g.color, m_g.pen));
    else setMesh(live, {}, Qt::black);
  }
  if (m_g.kind == Gesture::Draw && m_g.page >= 0 && m_g.page < int(m_pages.size())) {
    const Page& pg = m_pages[m_g.page];
    QMatrix4x4 lm = m;
    lm.translate((m_maxPageW - pg.w) / 2, pg.top);
    liveT->setMatrix(lm);
    liveT->markDirty(QSGNode::DirtyMatrix);
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

  // Eraser ring under the pointer (screen pixels).
  std::vector<QPointF> ringTris;
  if ((m_tool == "eraser" || (m_g.kind == Gesture::Core && m_g.stylus && m_hover.x() >= 0 && false)) && m_hover.x() >= 0) {
    const qreal r = m_widths.value("eraser", 14).toDouble() / 2 * m_zoom, t = 1.0;
    const int n = 48;
    for (int k = 0; k < n; ++k) {
      const double a0 = 2 * M_PI * k / n, a1 = 2 * M_PI * (k + 1) / n;
      const QPointF i0 = m_hover + QPointF(std::cos(a0), std::sin(a0)) * (r - t), o0 = m_hover + QPointF(std::cos(a0), std::sin(a0)) * r;
      const QPointF i1 = m_hover + QPointF(std::cos(a1), std::sin(a1)) * (r - t), o1 = m_hover + QPointF(std::cos(a1), std::sin(a1)) * r;
      ringTris.insert(ringTris.end(), {i0, o0, o1, i0, o1, i1});
    }
  }
  setMesh(ring, ringTris, QColor(90, 90, 90, 210));

  // (The laser is drawn by LaserTrail items over the canvas.)
  clearChildren(laser);
  setMesh(laser, {}, Qt::transparent);

  if (profile) qInfo("scene update %.2f ms", timer.nsecsElapsed() / 1e6);
  return root;
}
