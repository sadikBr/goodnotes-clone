// The web app's drawing and layout code (qt/js/bridge.js, global `Inkleaf`)
// running in a script engine, with a native canvas (JsCanvas) and the Rust
// core behind it. One engine for the app, on the GUI thread.
#pragma once

#include <list>
#include <QHash>
#include <QImage>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonValue>
#include <QObject>
#include <QRectF>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class JsCanvas;

/// What the scripts can ask the app for (`__native`).
class JsNative : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  Q_INVOKABLE QObject* canvas(int w, int h);
  Q_INVOKABLE QObject* image(const QString& src);
  Q_INVOKABLE QString uuid() const;
  /// Milliseconds on the app's clock (the same as pointer event times).
  Q_INVOKABLE double now() const;
  /// A core command; answers {"ok": …} or {"error": "…"} as JSON.
  Q_INVOKABLE QString call(const QString& cmd, const QString& argsJson);
  Q_INVOKABLE void log(const QString& s) const;
};

class Js : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(Js)
  QML_SINGLETON
public:
  static Js& instance();
  static Js* create(class QQmlEngine*, class QJSEngine*);

  QJSEngine& engine() { return m_engine; }
  /// The one clock for the app and its scripts (ms): pen samples and the
  /// scripts' timers must agree (holding a stroke still).
  static double clockMs();
  /// Inkleaf.<path> ("geometry.hitTest", "drawTemplate"…).
  QJSValue fn(const QString& path);
  /// Calls Inkleaf.<path>(args…); logs script errors.
  QJSValue call(const QString& path, const QJSValueList& args);
  /// JSON into script values (through JSON.parse: fast for big point lists).
  QJSValue toJs(const QJsonValue& v);
  QJSValue toJs(const QJsonObject& o) { return toJs(QJsonValue(o)); }
  QJSValue toJs(const QJsonArray& a) { return toJs(QJsonValue(a)); }
  QJsonValue toJson(const QJSValue& v);

  /// For QML: calls Inkleaf.<path> with plain values; returns plain values.
  Q_INVOKABLE QVariant run(const QString& path, const QVariantList& args = {});
  /** Calls Inkleaf.<path>(...) and waits for a promise it returns. */
  QJSValue awaitCall(const QString& path, const QJSValueList& args);

  /// A page's paper (template, colour) at `scale` pixels per page unit,
  /// only the `region` part of the page (page units), or all of it.
  QImage paper(const QString& tmpl, const QString& color, double w, double h, double scale, QRectF region = {});
  /// Elements (stored JSON objects' "data") drawn in order, the same way:
  /// `region` of the page at `scale`, on a transparent picture.
  QImage elements(const QJsonArray& els, double scale, QRectF region);
  /// Decoded pictures for image elements, by src (data: URL or asset id).
  QObject* imageFor(const QString& src);

private:
  Js();
  QJSEngine m_engine;
  QJSValue m_api;
  QJSValue m_images;
  QJSValue m_parse, m_stringify;  // Map src → native image, for drawElement
  QHash<QString, QObject*> m_imageCache;
  std::list<std::pair<QString, QImage>> m_papers;  // recent papers, newest first
};
