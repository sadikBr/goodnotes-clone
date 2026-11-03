#include "Js.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QUuid>
#include <QDebug>

#include "Core.h"
#include "JsCanvas.h"

// --- natives ---------------------------------------------------------------------

QObject* JsNative::canvas(int w, int h) { return new JsCanvas(w, h); }

QObject* JsNative::image(const QString& src) { return Js::instance().imageFor(src); }

double JsNative::now() const {
  static QElapsedTimer t = [] {
    QElapsedTimer e;
    e.start();
    return e;
  }();
  return double(t.nsecsElapsed()) / 1e6;
}

QString JsNative::uuid() const { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString JsNative::call(const QString& cmd, const QString& argsJson) {
  QString error;
  const QByteArray raw = InkleafCore::instance()->callRaw(cmd, argsJson.toUtf8(), &error);
  if (!error.isEmpty()) return QString::fromUtf8(QJsonDocument(QJsonObject{{"error", error}}).toJson(QJsonDocument::Compact));
  // {"ok": <answer>} without parsing the answer here.
  return QStringLiteral("{\"ok\":") + QString::fromUtf8(raw.isEmpty() ? QByteArray("null") : raw) + QStringLiteral("}");
}

void JsNative::log(const QString& s) const { qInfo().noquote() << "[js]" << s; }

// --- the engine --------------------------------------------------------------------

Js& Js::instance() {
  static Js* js = new Js;
  return *js;
}

Js* Js::create(QQmlEngine*, QJSEngine*) {
  QJSEngine::setObjectOwnership(&instance(), QJSEngine::CppOwnership);
  return &instance();
}

Js::Js() {
  m_engine.installExtensions(QJSEngine::ConsoleExtension);
  m_parse = m_engine.evaluate("JSON.parse");
  m_stringify = m_engine.evaluate("JSON.stringify");
  m_engine.globalObject().setProperty("__native", m_engine.newQObject(new JsNative(this)));
  QFile f(":/qt/qml/Inkleaf/js/bridge.js");
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning() << "bridge.js is missing";
    return;
  }
  const QJSValue r = m_engine.evaluate(QString::fromUtf8(f.readAll()), "bridge.js");
  if (r.isError()) qWarning().noquote() << "bridge.js:" << r.property("lineNumber").toInt() << r.toString();
  m_api = m_engine.globalObject().property("Inkleaf");
  m_images = m_engine.evaluate("new Map()");
  m_parse = m_engine.evaluate("JSON.parse");
  m_stringify = m_engine.evaluate("JSON.stringify");
}

QJSValue Js::fn(const QString& path) {
  QJSValue v = m_api;
  for (const QString& part : path.split('.')) v = v.property(part);
  return v;
}

QJSValue Js::call(const QString& path, const QJSValueList& args) {
  QJSValue f = fn(path);
  if (!f.isCallable()) {
    qWarning() << "no script function" << path;
    return {};
  }
  // `this` is the module object the function lives in.
  const int dot = path.lastIndexOf('.');
  const QJSValue self = dot < 0 ? m_api : fn(path.left(dot));
  QJSValue r = f.callWithInstance(self, args);
  if (r.isError()) qWarning().noquote() << "script error in" << path << ":" << r.property("lineNumber").toInt() << r.toString() << r.property("stack").toString();
  return r;
}

QJSValue Js::toJs(const QJsonValue& v) {
  if (v.isUndefined() || v.isNull()) return QJSValue(QJSValue::NullValue);
  if (!v.isObject() && !v.isArray()) return m_engine.toScriptValue(v.toVariant());
  const QByteArray json = v.isObject() ? QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact) : QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact);
  return m_parse.call({QString::fromUtf8(json)});
}

QJsonValue Js::toJson(const QJSValue& v) {
  if (v.isUndefined() || v.isNull()) return QJsonValue();
  if (!v.isObject()) return QJsonValue::fromVariant(v.toVariant());
  const QString s = m_stringify.call({v}).toString();
  const QJsonDocument d = QJsonDocument::fromJson(s.toUtf8());
  return d.isArray() ? QJsonValue(d.array()) : QJsonValue(d.object());
}

QVariant Js::run(const QString& path, const QVariantList& args) {
  QJSValueList list;
  for (const QVariant& a : args) list << m_engine.toScriptValue(a);
  return call(path, list).toVariant(QJSValue::ConvertJSObjects);
}

QObject* Js::imageFor(const QString& src) {
  if (auto it = m_imageCache.constFind(src); it != m_imageCache.constEnd()) return *it;
  QImage img;
  if (src.startsWith("data:")) {
    const int comma = src.indexOf(',');
    if (comma > 0) {
      const QByteArray raw = src.mid(comma + 1).toLatin1();
      img.loadFromData(src.left(comma).endsWith(";base64") ? QByteArray::fromBase64(raw) : QByteArray::fromPercentEncoding(raw));
    }
  } else if (!src.isEmpty()) {
    QString error;
    const QByteArray bytes = InkleafCore::instance()->callBytes("assetBytes", {{"id", src}}, &error);
    if (!bytes.isEmpty()) img.loadFromData(bytes);
  }
  if (img.isNull()) return nullptr;
  auto* o = new JsImage(img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  o->setParent(this);
  m_imageCache.insert(src, o);
  m_images.property("set").callWithInstance(m_images, QJSValueList{QJSValue(src), m_engine.newQObject(o)});
  return o;
}

QImage Js::paper(const QString& tmpl, const QString& color, double w, double h, double scale, QRectF region) {
  if (region.isEmpty()) region = QRectF(0, 0, w, h);
  auto* canvas = new JsCanvas(std::max(1, int(std::ceil(region.width() * scale))), std::max(1, int(std::ceil(region.height() * scale))));
  QJSValue ctx = m_engine.newQObject(canvas);
  canvas->setTransform(scale, 0, 0, scale, -region.x() * scale, -region.y() * scale);
  call("drawTemplate", {ctx, tmpl, w, h, color});
  QImage out = canvas->image();
  return out;
}

QImage Js::elements(const QJsonArray& els, double scale, QRectF region) {
  auto* canvas = new JsCanvas(std::max(1, int(std::ceil(region.width() * scale))), std::max(1, int(std::ceil(region.height() * scale))));
  QJSValue ctx = m_engine.newQObject(canvas);
  canvas->setTransform(scale, 0, 0, scale, -region.x() * scale, -region.y() * scale);
  QJSValue draw = fn("drawElement");
  QJSValue noop = m_engine.evaluate("(function () {})");
  for (const QJsonValue& v : els) {
    const QJsonObject data = v.toObject();
    if (data.value("kind").toString() == "image") imageFor(data.value("src").toString());
    QJSValue r = draw.call({ctx, toJs(data), m_images, noop});
    if (r.isError()) qWarning().noquote() << "drawElement:" << r.toString();
  }
  return canvas->image();
}
