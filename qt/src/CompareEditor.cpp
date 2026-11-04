// Developer and CI check while the editor moves from the script to C++
// (docs/MIGRATION.md, step 6): inkleaf --compare-editor <folder> runs scripted
// sessions (drawing, erasing, the lasso, moving, resizing, turning, text,
// sticky notes, undo, copying, instruments, holding a stroke still) through
// the script's editor (qt/js/editorCore.ts) and the C++ one (src/editor/
// EditorCore), and after every step compares what the app would show: each
// page's elements, the selection bar, the text being edited, the history,
// the instrument, the live transform, cursors and the overlay's picture.
#include "CompareEditor.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtDebug>
#include <cmath>
#include <functional>
#include <memory>

#include "Core.h"
#include "Js.h"
#include "JsCanvas.h"
#include "editor/EditorCore.h"

namespace {

/// One editor, driven the same way whichever it is.
struct Driver {
  virtual ~Driver() = default;
  virtual QString down(int pi, double x, double y, double p, double t, const QString& tool, bool shift) = 0;
  virtual void move(double x, double y, double p, double t, bool shift, bool alt) = 0;
  /// Any other editor method, with plain arguments.
  virtual QJsonValue call(const QString& method, const QVariantList& args) = 0;
  virtual void set(const QString& prop, const QVariant& v) = 0;
  /// What the app shows (ids not yet normalised).
  virtual QJsonObject state() = 0;
  virtual QImage overlay(int pi) = 0;
  /// Timers due (holding a stroke still).
  virtual void tick() {}
};

QJsonValue jsToJson(const QJSValue& v) {
  if (v.isUndefined()) return QJsonValue(QJsonValue::Undefined);
  const QString s = Js::instance().engine().evaluate("(v) => JSON.stringify(v === undefined ? null : v)").call({v}).toString();
  return QJsonDocument::fromJson(("[" + s + "]").toUtf8()).array().first();
}

struct ScriptDriver : Driver {
  QJSValue core;
  explicit ScriptDriver(const QJsonArray& pages) {
    Js& js = Js::instance();
    core = js.call("createEditor", {js.toJs(pages)});
  }
  QJSValue invoke(const QString& m, const QJSValueList& args) {
    QJSValue f = core.property(m);
    QJSValue r = f.callWithInstance(core, args);
    if (r.isError()) qWarning().noquote() << "script:" << m << r.toString();
    return r;
  }
  QString down(int pi, double x, double y, double p, double t, const QString& tool, bool shift) override { return invoke("down", {pi, x, y, p, t, tool, shift}).toString(); }
  void move(double x, double y, double p, double t, bool shift, bool alt) override { invoke("move", {x, y, p, t, shift, alt}); }
  QJsonValue call(const QString& m, const QVariantList& args) override {
    QJSValueList list;
    for (const QVariant& a : args) list << Js::instance().engine().toScriptValue(a);
    return jsToJson(invoke(m, list));
  }
  void set(const QString& prop, const QVariant& v) override { core.setProperty(prop, Js::instance().engine().toScriptValue(v)); }
  QJsonObject state() override {
    QJsonObject o;
    QJsonArray scenes, hidden;
    const int n = core.property("pages").property("length").toInt();
    for (int i = 0; i < n; ++i) {
      scenes.append(jsToJson(invoke("scene", {i})));
      QJsonArray h;
      for (const QJsonValue& el : scenes.last().toArray())
        if (invoke("isHidden", {i, el.toObject()["id"].toString()}).toBool()) h.append(el.toObject()["id"]);
      hidden.append(h);
    }
    o["scenes"] = scenes;
    o["hidden"] = hidden;
    o["active"] = core.property("active").toInt();
    o["selection"] = jsToJson(invoke("selectionInfo", {}));
    o["overlayBounds"] = jsToJson(invoke("overlayBounds", {}));
    o["liveHidden"] = invoke("liveHidden", {}).toBool();
    o["liveTransform"] = jsToJson(invoke("liveTransform", {}));
    o["editing"] = jsToJson(core.property("editing"));
    o["canUndo"] = invoke("canUndo", {}).toBool();
    o["canRedo"] = invoke("canRedo", {}).toBool();
    o["dirty"] = invoke("dirty", {}).toBool();
    QJsonObject ins = jsToJson(invoke("instrument", {})).toObject();
    ins.remove("rev");
    o["instrument"] = ins;
    return o;
  }
  QImage overlay(int pi) override {
    auto* c = new JsCanvas(794, 1123);
    QJSEngine::setObjectOwnership(c, QJSEngine::CppOwnership);
    QJSValue ctx = Js::instance().engine().newQObject(c);
    invoke("drawOverlay", {ctx, pi});
    QImage img = c->image();
    delete c;
    return img;
  }
  void tick() override {
    QJSValue g = Js::instance().engine().globalObject();
    g.property("__runTimers").call();
  }
};

QJsonValue rectJson(const std::optional<ed::Rect>& r) {
  if (!r) return QJsonValue(QJsonValue::Null);
  return QJsonObject{{"x", r->x}, {"y", r->y}, {"w", r->w}, {"h", r->h}};
}

struct NativeDriver : Driver {
  ed::EditorCore core;
  explicit NativeDriver(const QJsonArray& pages) : core(pages) {}
  QString down(int pi, double x, double y, double p, double t, const QString& tool, bool shift) override { return core.down(pi, x, y, p, t, tool, shift); }
  void move(double x, double y, double p, double t, bool shift, bool alt) override { core.move(x, y, p, t, shift, alt); }
  QJsonValue call(const QString& m, const QVariantList& a) override {
    auto s = [&](int i) { return a.value(i).toString(); };
    auto d = [&](int i) { return a.value(i).toDouble(); };
    if (m == "up") core.up();
    else if (m == "cancel") core.cancel();
    else if (m == "hover") core.hover(d(0), d(1));
    else if (m == "cursorAt") return core.cursorAt(d(0), d(1));
    else if (m == "selectionInfo") return QJsonObject::fromVariantMap(core.selectionInfo());
    else if (m == "activate") core.activate(a.value(0).toInt());
    else if (m == "undo") core.undo();
    else if (m == "redo") core.redo();
    else if (m == "selectAll") core.selectAll();
    else if (m == "clearSelection") core.clearSelection();
    else if (m == "deleteSelection") core.deleteSelection();
    else if (m == "duplicateSelection") core.duplicateSelection();
    else if (m == "copySelection") return core.copySelection();
    else if (m == "cutSelection") return core.cutSelection();
    else if (m == "pasteClipboard") return core.pasteClipboard();
    else if (m == "recolorSelection") core.recolorSelection(s(0));
    else if (m == "styleSelectedText") core.styleSelectedText(a.value(0).toMap());
    else if (m == "updateSelectedShape") core.updateSelectedShape(s(0), a.size() > 1 ? d(1) : NAN);
    else if (m == "updateEdit") core.updateEdit(s(0));
    else if (m == "applyFormat") core.applyFormat(a.value(0).toMap());
    else if (m == "finishTextEdit") core.finishTextEdit();
    else if (m == "insertImage") core.insertImage(s(0), d(1), d(2), d(3), d(4));
    else if (m == "inkToText") return core.inkToText(s(0), s(1), a.value(2).toBool());
    else if (m == "selectedInkStrokes") return QJsonValue::fromVariant(core.selectedInkStrokes());
    else if (m == "continueList") {
      const QVariantMap r = ed::continueListFor(s(0), a.value(1).toInt());
      return r.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue::fromVariant(r);
    } else if (m == "showInstrument") core.showInstrument(s(0), d(1), d(2));
    else if (m == "hideInstrument") core.hideInstrument();
    else if (m == "placeInstrument") core.placeInstrument(d(0), d(1), d(2));
    else if (m == "setArm") core.setArm(a.value(0).toInt(), d(1));
    else if (m == "instrumentContains") return core.instrumentContains(d(0), d(1));
    else if (m == "takeDirty") {
      QJsonArray out;
      for (int i : core.takeDirty()) out.append(i);
      return out;
    } else if (m == "forgetAll") core.forgetAll();
    else qWarning().noquote() << "native: no method" << m;
    return QJsonValue(QJsonValue::Undefined);
  }
  void set(const QString& prop, const QVariant& v) override {
    if (prop == "tool") core.tool = v.toString();
    else if (prop == "shapeKind") core.shapeKind = v.toString();
    else if (prop == "shapeFill") core.shapeFill = v.toBool();
    else if (prop == "eraserMode") core.eraserMode = v.toString();
    else if (prop == "penStyle") core.penStyle = v.toString();
    else if (prop == "streamline") core.streamline = v.toDouble();
    else if (prop == "textFont") core.textFont = v.toString();
    else if (prop == "zoom") core.zoom = v.toDouble();
    else if (prop == "viewWidth") core.viewWidth = v.toDouble();
    else if (prop == "colors") {
      const QVariantMap m = v.toMap();
      for (auto it = m.begin(); it != m.end(); ++it) core.colors[it.key()] = it.value().toString();
    } else if (prop == "widths") {
      const QVariantMap m = v.toMap();
      for (auto it = m.begin(); it != m.end(); ++it) core.widths[it.key()] = it.value().toDouble();
    } else qWarning().noquote() << "native: no property" << prop;
  }
  QJsonObject state() override {
    QJsonObject o;
    QJsonArray scenes, hidden;
    for (int i = 0; i < int(core.pages.size()); ++i) {
      QJsonArray s, h;
      for (const ed::ElPtr& el : core.load(i)) {
        s.append(el->toJson());
        if (core.isHidden(i, el->id)) h.append(el->id);
      }
      scenes.append(s);
      hidden.append(h);
    }
    o["scenes"] = scenes;
    o["hidden"] = hidden;
    o["active"] = core.active;
    o["selection"] = QJsonObject::fromVariantMap(core.selectionInfo());
    o["overlayBounds"] = rectJson(core.overlayBounds());
    o["liveHidden"] = core.liveHidden();
    if (const auto lt = core.liveTransform()) {
      QJsonArray ids;
      for (const QString& id : lt->ids) ids.append(id);
      o["liveTransform"] = QJsonObject{{"ids", ids}, {"m", QJsonObject{{"a", lt->m.a}, {"b", lt->m.b}, {"c", lt->m.c}, {"d", lt->m.d}, {"e", lt->m.e}, {"f", lt->m.f}}}};
    } else {
      o["liveTransform"] = QJsonValue(QJsonValue::Null);
    }
    o["editing"] = core.editing() ? QJsonValue(core.editing()->toJson()) : QJsonValue(QJsonValue::Null);
    o["canUndo"] = core.canUndo();
    o["canRedo"] = core.canRedo();
    o["dirty"] = core.dirty();
    QJsonObject ins = QJsonObject::fromVariantMap(core.instrument());
    ins.remove("rev");
    o["instrument"] = ins;
    return o;
  }
  QImage overlay(int pi) override {
    JsCanvas c(794, 1123);
    core.drawOverlay(c, pi);
    return c.image();
  }
};

/// Element and other ids replaced by their order of first appearance.
void normalise(QJsonValue& v, QHash<QString, QString>& ids) {
  auto map = [&](const QString& id) {
    if (!ids.contains(id)) ids.insert(id, QString("#%1").arg(ids.size() + 1));
    return ids.value(id);
  };
  if (v.isObject()) {
    QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
      QJsonValue c = it.value();
      if (it.key() == "id" && c.isString()) c = map(c.toString());
      else if ((it.key() == "ids" || it.key() == "hidden") && c.isArray()) {
        QJsonArray a = c.toArray();
        for (int k = 0; k < a.size(); ++k) {
          if (a[k].isString()) a[k] = map(a[k].toString());
          else if (a[k].isArray()) {
            QJsonArray b = a[k].toArray();
            for (int q = 0; q < b.size(); ++q) b[q] = map(b[q].toString());
            a[k] = b;
          }
        }
        c = a;
      } else normalise(c, ids);
      it.value() = c;
    }
    v = o;
  } else if (v.isArray()) {
    QJsonArray a = v.toArray();
    for (int k = 0; k < a.size(); ++k) {
      QJsonValue c = a[k];
      normalise(c, ids);
      a[k] = c;
    }
    v = a;
  }
}

/// Differences between two JSON values (numbers within a hair).
void compare(const QJsonValue& a, const QJsonValue& b, const QString& path, QStringList& out) {
  if (out.size() > 12) return;
  if (a.isDouble() && b.isDouble()) {
    const double x = a.toDouble(), y = b.toDouble();
    if (std::abs(x - y) > 1e-6 * std::max(1.0, std::abs(x))) out << QString("%1: %2 vs %3").arg(path).arg(x, 0, 'g', 12).arg(y, 0, 'g', 12);
    return;
  }
  if (a.type() != b.type()) {
    // A key missing on one side counts as null (JSON.stringify drops undefined).
    if ((a.isNull() || a.isUndefined()) && (b.isNull() || b.isUndefined())) return;
    out << QString("%1: %2 vs %3").arg(path, QString::fromUtf8(QJsonDocument(QJsonArray{a}).toJson(QJsonDocument::Compact)).left(160), QString::fromUtf8(QJsonDocument(QJsonArray{b}).toJson(QJsonDocument::Compact)).left(160));
    return;
  }
  if (a.isObject()) {
    const QJsonObject x = a.toObject(), y = b.toObject();
    QStringList keys = x.keys();
    for (const QString& k : y.keys())
      if (!keys.contains(k)) keys << k;
    for (const QString& k : keys) compare(x.value(k), y.value(k), path + "." + k, out);
    return;
  }
  if (a.isArray()) {
    const QJsonArray x = a.toArray(), y = b.toArray();
    if (x.size() != y.size()) out << QString("%1: %2 vs %3 items").arg(path).arg(x.size()).arg(y.size());
    for (int k = 0; k < std::min(x.size(), y.size()); ++k) compare(x[k], y[k], QString("%1[%2]").arg(path).arg(k), out);
    return;
  }
  // The instrument's picture: the same SVG, spacing aside.
  static const QRegularExpression between(QStringLiteral(">\\s+<"));
  // (-0.00 and 0.00 draw the same.)
  auto tidy = [](QString t) { return t.replace(between, "><").replace("-0.00", "0.00").simplified(); };
  if (a.isString() && b.isString() && tidy(a.toString()) == tidy(b.toString())) return;
  if (a.isString() && b.isString() && a != b) {
    // Where they part.
    const QString x = tidy(a.toString()), y = tidy(b.toString());
    int k = 0;
    while (k < x.size() && k < y.size() && x[k] == y[k]) ++k;
    const int from = std::max(0, k - 40);
    out << QString("%1 (at %2): …%3… vs …%4…").arg(path).arg(k).arg(x.mid(from, 120), y.mid(from, 120));
    return;
  }
  if (a != b) out << QString("%1: %2 vs %3").arg(path, a.toVariant().toString().left(120), b.toVariant().toString().left(120));
}

int pixelDiff(const QImage& a0, const QImage& b0, int* max) {
  const QImage a = a0.convertToFormat(QImage::Format_ARGB32), b = b0.convertToFormat(QImage::Format_ARGB32);
  if (a.size() != b.size()) return -1;
  int n = 0;
  *max = 0;
  for (int y = 0; y < a.height(); ++y) {
    const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      const int m = std::max({std::abs(qRed(pa[x]) - qRed(pb[x])), std::abs(qGreen(pa[x]) - qGreen(pb[x])), std::abs(qBlue(pa[x]) - qBlue(pb[x])), std::abs(qAlpha(pa[x]) - qAlpha(pb[x]))});
      *max = std::max(*max, m);
      if (m > 2) ++n;
    }
  }
  return n;
}

// ------------------------------------------------------------ the sessions

/// What a session does, step by step, to both editors at once.
struct Session {
  QString name;
  std::function<void(class Runner&)> run;
};

class Runner {
public:
  Runner(const QString& name, const QJsonArray& pages, const QString& outDir) : m_name(name), m_out(outDir) {
    m_eds.emplace_back(new ScriptDriver(pages));
    m_eds.emplace_back(new NativeDriver(pages));
    for (auto& e : m_eds) {
      e->set("colors", QVariantMap{{"pen", "#111827"}, {"highlighter", "#facc15"}, {"shape", "#2563eb"}, {"text", "#111827"}, {"sticky", "#fef08a"}, {"laser", "#ff1f1f"}});
      e->set("widths", QVariantMap{{"pen", 3}, {"highlighter", 18}, {"eraser", 14}, {"shape", 3}, {"text", 20}, {"sticky", 18}});
      e->set("viewWidth", 1000);
    }
  }
  int failures = 0, steps = 0;

  void set(const QString& prop, const QVariant& v) {
    for (auto& e : m_eds) e->set(prop, v);
  }
  void down(int pi, double x, double y, const QString& tool, bool shift = false) {
    const double t = Js::clockMs();
    QStringList r;
    for (auto& e : m_eds) r << e->down(pi, x, y, 0.5, t, tool, shift);
    check(QString("down %1 %2,%3").arg(tool).arg(x).arg(y), r[0] != r[1] ? QStringList{QString("returned %1 vs %2").arg(r[0], r[1])} : QStringList{});
  }
  void move(double x, double y, bool shift = false, bool alt = false, double p = 0.5) {
    const double t = Js::clockMs();
    for (auto& e : m_eds) e->move(x, y, p, t, shift, alt);
  }
  /// A drag from the press point along `pts`, then up.
  void drag(const std::vector<ed::P2>& pts, bool shift = false, bool alt = false) {
    for (const auto& p : pts) move(p[0], p[1], shift, alt);
    check("drag", {});
  }
  void stroke(int pi, const QString& tool, const std::vector<ed::P2>& pts, bool shift = false) {
    down(pi, pts[0][0], pts[0][1], tool, shift);
    for (size_t k = 1; k < pts.size(); ++k) move(pts[k][0], pts[k][1], shift, false, 0.3 + 0.4 * std::sin(k * 0.3) * std::sin(k * 0.3));
    check(tool + " moves", {});
    call("up");
  }
  QJsonValue call(const QString& m, const QVariantList& args = {}) {
    QJsonValue r[2];
    for (int k = 0; k < 2; ++k) r[k] = m_eds[k]->call(m, args);
    QStringList d;
    QHash<QString, QString> i0, i1;
    normalise(r[0], i0);
    normalise(r[1], i1);
    compare(r[0], r[1], "result", d);
    check(m, d);
    return r[1];
  }
  /// Real time passes (timers fire).
  void wait(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
      for (auto& e : m_eds) e->tick();
    }
    check(QString("wait %1").arg(ms), {});
  }
  void check(const QString& step, QStringList diffs) {
    ++steps;
    QJsonValue s[2] = {m_eds[0]->state(), m_eds[1]->state()};
    QHash<QString, QString> i0, i1;
    normalise(s[0], i0);
    normalise(s[1], i1);
    compare(s[0], s[1], "state", diffs);
    const int pi = s[1].toObject()["active"].toInt();
    const QImage a = m_eds[0]->overlay(pi), b = m_eds[1]->overlay(pi);
    int max = 0;
    const int px = pixelDiff(a, b, &max);
    if (px != 0) diffs << QString("overlay: %1 pixels differ (max %2)").arg(px).arg(max);
    if (diffs.isEmpty()) return;
    ++failures;
    qWarning().noquote() << "DIFFERS" << m_name << "step" << steps << step;
    for (const QString& d : diffs) qWarning().noquote() << "   " << d;
    if (px != 0) {
      const QString base = QString("%1/%2-%3").arg(m_out, m_name).arg(steps);
      a.save(base + "-script.png");
      b.save(base + "-native.png");
    }
  }

private:
  QString m_name, m_out;
  std::vector<std::unique_ptr<Driver>> m_eds;
};

std::vector<ed::P2> line(ed::P2 a, ed::P2 b, int n) {
  std::vector<ed::P2> out;
  for (int k = 0; k <= n; ++k) out.push_back({a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n});
  return out;
}
std::vector<ed::P2> wobbly(double x, double y, double w, int n, double seed) {
  std::vector<ed::P2> out;
  for (int k = 0; k <= n; ++k) {
    const double t = double(k) / n;
    out.push_back({x + t * w, y + 14 * std::sin(t * 9 + seed) + 4 * std::cos(t * 23 + seed)});
  }
  return out;
}
std::vector<ed::P2> loop(double cx, double cy, double rx, double ry, int n, double wob = 0, double turns = 1.04) {
  std::vector<ed::P2> out;
  for (int k = 0; k <= n; ++k) {
    const double a = turns * 2 * M_PI * k / n;
    out.push_back({cx + rx * std::cos(a) * (1 + wob * std::sin(5 * a)), cy + ry * std::sin(a) * (1 + wob * std::cos(3 * a))});
  }
  return out;
}
std::vector<ed::P2> roughRect(double x, double y, double w, double h) {
  std::vector<ed::P2> out;
  auto seg = [&](ed::P2 a, ed::P2 b) {
    for (int k = 0; k < 12; ++k) out.push_back({a[0] + (b[0] - a[0]) * k / 12 + std::sin(k * 1.7) * 1.5, a[1] + (b[1] - a[1]) * k / 12 + std::cos(k * 2.3) * 1.5});
  };
  seg({x, y}, {x + w, y});
  seg({x + w, y}, {x + w, y + h});
  seg({x + w, y + h}, {x, y + h});
  seg({x, y + h}, {x, y + 4});
  out.push_back({x + 1, y + 2});
  return out;
}

std::vector<Session> sessions() {
  std::vector<Session> out;
  out.push_back({"ink", [](Runner& r) {
                   for (const char* style : {"fountain", "ballpoint", "pencil", "calligraphy", "brush"}) {
                     r.set("penStyle", style);
                     r.stroke(0, "pen", wobbly(80, 640, 300, 50, style[0]));
                   }
                   r.set("streamline", 0.8);
                   r.stroke(0, "pen", wobbly(80, 720, 300, 80, 2));
                   r.stroke(0, "highlighter", wobbly(80, 780, 300, 40, 3));
                   r.stroke(0, "highlighter", line({90, 820}, {400, 822}, 30));  // straightened
                   r.stroke(0, "pen", {{500, 700}});                              // a dot
                   r.stroke(0, "pen", line({100, 1050}, {130, 1053}, 90));         // close points: thinned
                   r.call("undo");
                   r.call("undo");
                   r.call("redo");
                   r.set("zoom", 2.5);
                   r.stroke(0, "pen", wobbly(420, 900, 120, 70, 4));
                   r.set("zoom", 1);
                 }});
  out.push_back({"shapes", [](Runner& r) {
                   r.stroke(0, "shape", roughRect(420, 600, 180, 110));
                   r.stroke(0, "shape", loop(520, 820, 70, 45, 60, 0.03));
                   r.stroke(0, "shape", line({100, 950}, {380, 990}, 25));
                   r.stroke(0, "shape", {{100, 1000}, {160, 1050}, {210, 1000}, {260, 1050}, {300, 1001}});
                   for (const char* k : {"line", "arrow", "rectangle", "ellipse", "triangle"}) {
                     r.set("shapeKind", k);
                     r.down(0, 450, 950, "shape");
                     r.drag(line({450, 950}, {560, 1040}, 8));
                     r.drag(line({560, 1040}, {600, 1000}, 3), true);
                     r.call("up");
                   }
                   r.set("shapeFill", true);
                   r.set("shapeKind", "rectangle");
                   r.down(0, 620, 950, "shape");
                   r.drag(line({620, 950}, {700, 1060}, 5));
                   r.call("up");
                   r.set("shapeKind", "auto");
                   r.call("undo");
                 }});
  out.push_back({"erase", [](Runner& r) {
                   r.stroke(0, "pen", wobbly(80, 640, 300, 60, 1));
                   r.stroke(0, "shape", roughRect(420, 600, 180, 110));
                   r.stroke(0, "eraser", line({200, 600}, {210, 700}, 12));  // through the stroke
                   r.stroke(0, "eraser", line({500, 580}, {510, 730}, 12));  // through the rectangle
                   r.stroke(0, "eraser", line({90, 140}, {300, 145}, 12));   // over text: whole box
                   r.call("undo");
                   r.set("eraserMode", "stroke");
                   r.stroke(0, "eraser", line({100, 560}, {120, 760}, 10));
                   r.set("eraserMode", "precise");
                   r.stroke(0, "eraser", line({600, 50}, {700, 60}, 10));  // nothing there
                   r.call("undo");
                   r.call("redo");
                 }});
  out.push_back({"lasso", [](Runner& r) {
                   r.set("tool", "lasso");
                   r.stroke(0, "lasso", loop(330, 300, 300, 260, 60, 0, 1));
                   for (const auto& p : std::vector<ed::P2>{{200, 200}, {40, 40}, {640, 570}, {330, 580}, {50, 300}, {700, 300}})
                     r.call("cursorAt", {p[0], p[1]});
                   // Move it (text snaps to the ruled lines; Alt doesn't).
                   r.down(0, 300, 200, "lasso");
                   r.drag(line({300, 200}, {343, 251}, 8));
                   r.call("up");
                   r.down(0, 300, 240, "lasso");
                   r.drag(line({300, 240}, {330, 263}, 6), false, true);
                   r.call("up");
                   r.call("undo");
                   // Corner, side and turning handles.
                   const QJsonObject box = r.call("selectionInfo").toObject()["box"].toObject();
                   const double bx = box["x"].toDouble(), by = box["y"].toDouble(), bw = box["w"].toDouble(), bh = box["h"].toDouble();
                   r.down(0, bx + bw + 6, by + bh + 6, "lasso");
                   r.drag(line({bx + bw + 6, by + bh + 6}, {bx + bw + 60, by + bh + 40}, 6));
                   r.call("up");
                   r.call("undo");
                   r.down(0, bx + bw + 6, by + bh / 2, "lasso");
                   r.drag(line({bx + bw + 6, by + bh / 2}, {bx + bw - 80, by + bh / 2}, 6), true);
                   r.call("up");
                   r.call("undo");
                   r.call("cursorAt", {bx + bw / 2, by + bh + 6 + 26});
                   r.down(0, bx + bw / 2, by + bh + 6 + 26, "lasso");
                   r.drag(line({bx + bw / 2, by + bh + 32}, {bx + bw + 100, by + bh / 2}, 8));
                   r.call("up");
                   r.call("undo");
                   // A tap selects one thing; off everything clears.
                   r.stroke(0, "lasso", {{760, 1100}});
                   r.stroke(0, "lasso", {{120, 132}});
                   r.call("selectAll");
                   r.call("deleteSelection");
                   r.call("undo");
                 }});
  out.push_back({"text-only transform", [](Runner& r) {
                   r.stroke(0, "lasso", loop(260, 160, 240, 80, 50, 0, 1));
                   const QJsonObject box = r.call("selectionInfo").toObject()["box"].toObject();
                   const double bx = box["x"].toDouble(), by = box["y"].toDouble(), bw = box["w"].toDouble(), bh = box["h"].toDouble();
                   r.down(0, bx + bw + 6, by + bh / 2, "lasso");
                   r.drag(line({bx + bw + 6, by + bh / 2}, {bx + bw - 120, by + bh / 2}, 8));
                   r.call("up");
                   r.down(0, bx + bw + 6 - 120, by + bh + 6, "lasso");
                   r.drag(line({bx + bw - 114, by + bh + 6}, {bx + bw - 60, by + bh + 50}, 6));
                   r.call("up");
                   r.call("styleSelectedText", {QVariantMap{{"bold", true}}});
                   r.call("styleSelectedText", {QVariantMap{{"scale", 1.25}, {"align", "center"}}});
                   r.call("styleSelectedText", {QVariantMap{{"list", "bullet"}}});
                   r.call("styleSelectedText", {QVariantMap{{"font", "caveat"}, {"italic", true}}});
                   r.call("recolorSelection", {"#dc2626"});
                   r.call("copySelection");
                   r.call("pasteClipboard");
                   r.call("duplicateSelection");
                   r.call("cutSelection");
                   r.call("undo");
                 }});
  out.push_back({"line handles", [](Runner& r) {
                   r.stroke(0, "lasso", {{240, 470}});  // the seeded arrow
                   r.call("cursorAt", {160, 460});
                   r.call("cursorAt", {240, 470});
                   r.down(0, 320, 480, "lasso");
                   r.drag(line({320, 480}, {420, 520}, 6));
                   r.drag(line({420, 520}, {440, 560}, 3), true);
                   r.call("up");
                   r.call("updateSelectedShape", {"width", 7});
                   r.call("updateSelectedShape", {"line"});
                   r.call("recolorSelection", {"#16a34a"});
                   r.call("clearSelection");
                   r.stroke(0, "lasso", {{560, 400}});  // the seeded rectangle
                   r.call("updateSelectedShape", {"fill"});
                   r.call("updateSelectedShape", {"ellipse"});
                   r.call("updateSelectedShape", {"triangle"});
                   r.call("recolorSelection", {"#7c3aed"});
                   r.down(0, 640, 470, "lasso");
                   r.drag(line({640, 470}, {700, 520}, 5));
                   r.call("up");
                   r.call("undo");
                   r.call("undo");
                 }});
  out.push_back({"text", [](Runner& r) {
                   r.set("tool", "text");
                   r.down(0, 120, 560, "text");
                   r.call("updateEdit", {"Hello"});
                   r.call("updateEdit", {"Hello there, this is a longer line that wraps around the box"});
                   r.call("applyFormat", {QVariantMap{{"bold", true}}});
                   r.call("applyFormat", {QVariantMap{{"align", "center"}, {"fontSize", 26}}});
                   r.call("continueList", {"- one", 5});
                   r.call("continueList", {"1. one\n2. two", 13});
                   r.call("continueList", {"- ", 2});
                   r.call("finishTextEdit");
                   r.down(0, 400, 600, "text");  // an empty box goes
                   r.call("finishTextEdit");
                   r.down(0, 500, 700, "text");
                   r.call("updateEdit", {QString::fromUtf8("مرحبا بكم في الدرس")});
                   r.call("finishTextEdit");
                   r.down(0, 130, 130, "text");  // the seeded text box
                   r.call("updateEdit", {"Changed text   "});
                   r.call("applyFormat", {QVariantMap{{"italic", true}, {"color", "#2563eb"}, {"font", "serif"}}});
                   r.call("finishTextEdit");
                   r.down(0, 130, 130, "text");  // opened and closed unchanged: no history step
                   r.call("finishTextEdit");
                   r.call("undo");
                   r.call("undo");
                   r.call("redo");
                   r.set("tool", "sticky");
                   r.down(0, 600, 800, "sticky");
                   r.call("updateEdit", {"Remember\nthe homework"});
                   r.call("finishTextEdit");
                   r.down(0, 610, 820, "sticky");  // the same note again
                   r.call("updateEdit", {""});
                   r.call("finishTextEdit");
                   r.call("undo");
                 }});
  out.push_back({"pages and pictures", [](Runner& r) {
                   r.stroke(1, "pen", wobbly(80, 200, 300, 40, 1));
                   r.call("activate", {0});
                   r.stroke(0, "pen", wobbly(80, 300, 300, 40, 2));
                   r.call("undo");
                   r.call("undo");  // back on page 2
                   r.call("redo");
                   r.call("insertImage", {"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==", 1200, 800, 300, 400});
                   r.call("takeDirty");
                   r.call("forgetAll");
                 }});
  out.push_back({"handwriting to text", [](Runner& r) {
                   r.stroke(0, "pen", wobbly(100, 600, 200, 40, 1));
                   r.set("colors", QVariantMap{{"pen", "#dc2626"}});
                   r.stroke(0, "pen", wobbly(100, 640, 220, 60, 2));
                   r.stroke(0, "lasso", loop(210, 630, 160, 70, 40, 0, 1));
                   r.call("selectedInkStrokes");
                   r.call("inkToText", {"hello world", "caveat", false});
                   r.call("undo");
                   r.stroke(0, "lasso", loop(210, 630, 160, 70, 40, 0, 1));
                   r.call("inkToText", {"two\nlines", "sans", true});
                 }});
  out.push_back({"instruments", [](Runner& r) {
                   for (const char* k : {"ruler", "protractor", "square45", "square60"}) {
                     r.call("showInstrument", {k, 400, 500});
                     r.call("instrumentContains", {400, 500});
                     r.call("instrumentContains", {50, 50});
                   }
                   r.call("hideInstrument");
                   r.call("showInstrument", {"ruler", 400, 500});
                   r.call("placeInstrument", {380, 520, 0.3});
                   // Along the ruler's edge: pen, highlighter and the shape tool.
                   for (const char* tool : {"pen", "highlighter", "shape"}) {
                     r.down(0, 300, 470, tool);
                     r.drag(line({300, 470}, {520, 540}, 12));
                     r.call("up");
                   }
                   r.call("hideInstrument");
                   r.call("showInstrument", {"protractor", 400, 700});
                   r.call("setArm", {0, 45});
                   r.call("setArm", {1, 200});
                   r.call("placeInstrument", {400, 700, 0});
                   for (const char* tool : {"pen", "shape"}) {
                     r.down(0, 400 + 150, 700 - 4, tool);
                     std::vector<ed::P2> arc;
                     for (int k = 0; k <= 20; ++k) arc.push_back({400 + 150 * std::cos(-k * 0.08), 700 + 150 * std::sin(-k * 0.08)});
                     r.drag(arc);
                     r.call("up");
                   }
                   r.call("cancel");
                 }});
  out.push_back({"holding still", [](Runner& r) {
                   // A circle held at the end becomes one, then turns and grows with the pen.
                   const auto circle = loop(300, 700, 80, 60, 50, 0.04);
                   r.down(0, circle[0][0], circle[0][1], "pen");
                   for (size_t k = 1; k < circle.size(); ++k) r.move(circle[k][0], circle[k][1]);
                   r.wait(800);
                   r.drag(line(circle.back(), {420, 760}, 6));
                   r.wait(900);  // still again: now it moves
                   r.drag(line({420, 760}, {460, 700}, 6));
                   r.call("up");
                   // A highlighter line held straight.
                   r.down(0, 100, 900, "highlighter");
                   for (const auto& p : line({100, 900}, {400, 903}, 20)) r.move(p[0], p[1]);
                   r.wait(800);
                   r.call("up");
                   // A plus sign: two strokes, the second held.
                   r.stroke(0, "pen", line({600, 600}, {600, 680}, 12));
                   r.down(0, 560, 640, "pen");
                   for (const auto& p : line({560, 640}, {640, 641}, 12)) r.move(p[0], p[1]);
                   r.wait(800);
                   r.call("up");
                   // Cancelled while held.
                   r.down(0, 300, 950, "pen");
                   for (const auto& p : roughRect(300, 950, 120, 80)) r.move(p[0], p[1]);
                   r.wait(800);
                   r.call("cancel");
                 }});
  return out;
}

/// The library the sessions start from: two ruled pages with text, ink, shapes and a sticky note.
QJsonArray seed() {
  QString err;
  InkleafCore* core = InkleafCore::instance();
  const QJsonObject nb = core->callJson("createNotebook", {{"input", QJsonObject{{"title", "Compare"}, {"folderId", QJsonValue()}, {"coverColor", "#2563eb"}, {"template", "ruled"}, {"paperColor", "#ffffff"}}}}, &err).toObject();
  core->callJson("addPage", {{"notebookId", nb["id"]}, {"index", QJsonValue()}, {"template", "grid"}}, &err);
  QJsonArray pages = core->callJson("pages", {{"notebookId", nb["id"]}}, &err).toArray();
  // Ignore any cover page: the notebook's first pages to write on.
  QJsonArray writing;
  for (const auto& p : pages)
    if (!p.toObject()["template"].toString().startsWith("cover")) writing.append(p);
  auto row = [](const QString& id, double z, QJsonObject data) {
    data["id"] = id;
    data["kind"] = data["kind"].toString();
    return QJsonObject{{"id", id}, {"kind", data["kind"]}, {"z", z}, {"data", data}};
  };
  QJsonArray scribble;
  for (int k = 0; k <= 40; ++k) scribble.append(QJsonArray{360 + k * 6.0, 330 + 12 * std::sin(k * 0.4), 0.5});
  const QJsonArray els{
      row("t1", 0, {{"kind", "text"}, {"x", 100}, {"y", 118}, {"w", 380}, {"text", "Notes for the lesson on fractions"}, {"fontSize", 20}, {"color", "#111827"}, {"font", "sans"}}),
      row("t2", 1, {{"kind", "text"}, {"x", 102}, {"y", 190}, {"w", 300}, {"text", "- halves\n- quarters\n- thirds"}, {"fontSize", 18}, {"color", "#111827"}, {"font", "sans"}}),
      row("s1", 2, {{"kind", "stroke"}, {"tool", "pen"}, {"style", "fountain"}, {"color", "#111827"}, {"width", 3}, {"points", scribble}, {"smooth", 0.38}}),
      row("a1", 3, {{"kind", "shape"}, {"shape", QJsonObject{{"kind", "arrow"}, {"points", QJsonArray{QJsonArray{160, 460}, QJsonArray{320, 480}}}}}, {"color", "#2563eb"}, {"width", 3}, {"fill", QJsonValue()}}),
      row("r1", 4, {{"kind", "shape"}, {"shape", QJsonObject{{"kind", "rectangle"}, {"points", QJsonArray{QJsonArray{520, 380}, QJsonArray{640, 380}, QJsonArray{640, 470}, QJsonArray{520, 470}}}}}, {"color", "#dc2626"}, {"width", 2}, {"fill", QJsonValue()}}),
      row("n1", 5, {{"kind", "sticky"}, {"x", 560}, {"y", 120}, {"w", 180}, {"h", 160}, {"text", "Bring rulers"}, {"color", "#fef08a"}, {"font", "sans"}, {"fontSize", 18}}),
  };
  core->callJson("saveElements", {{"pageId", writing[0].toObject()["id"]}, {"elements", els}}, &err);
  if (!err.isEmpty()) qWarning().noquote() << "seed:" << err;
  return writing;
}

}  // namespace

int compareEditor(const QString& outDir) {
  QDir().mkpath(outDir);
  QTemporaryDir data;
  qputenv("INKLEAF_DATA_DIR", data.path().toUtf8());
  const QJsonArray pages = seed();
  int failed = 0, steps = 0;
  for (const Session& s : sessions()) {
    Runner r(s.name, pages, outDir);
    r.check("start", {});
    s.run(r);
    failed += r.failures;
    steps += r.steps;
    Js::instance().engine().collectGarbage();
  }
  qInfo().noquote() << QString("compare-editor: %1 of %2 steps differ").arg(failed).arg(steps);
  return failed ? 1 : 0;
}
