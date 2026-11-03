#include "PaperImage.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include "Js.h"
#include "JsCanvas.h"

QImage PaperImage::requestImage(const QString& id, QSize* size, const QSize& requested) {
  // The script engine lives on the GUI thread.
  if (QThread::currentThread() != qApp->thread()) {
    QImage out;
    QMetaObject::invokeMethod(qApp, [&] { out = requestImage(id, size, requested); }, Qt::BlockingQueuedConnection);
    return out;
  }
  const QStringList parts = id.split('/');
  const QString tmpl = parts.value(0, "blank");
  const QString color = "#" + parts.value(1, "fffdf7");
  const bool whole = parts.value(2) == "whole";
  const QSize want = requested.isValid() && !requested.isEmpty() ? requested : QSize(176, 220);
  QImage img;
  if (whole) {
    img = Js::instance().paper(tmpl, color, 794, 1123, want.width() / 794.0);
    img = img.copy(0, 0, want.width(), want.height());
  } else {
    // The page's top-left, a little above the first ruled line (300 units across).
    const double scale = want.width() / 300.0;
    img = Js::instance().paper(tmpl, color, 794, 1123, scale, QRectF(40, 70, 300, want.height() / scale));
  }
  if (size) *size = img.size();
  return img;
}

QImage InkSampleImage::requestImage(const QString& id, QSize* size, const QSize& requested) {
  if (QThread::currentThread() != qApp->thread()) {
    QImage out;
    QMetaObject::invokeMethod(qApp, [&] { out = requestImage(id, size, requested); }, Qt::BlockingQueuedConnection);
    return out;
  }
  const QStringList p = id.split('/');
  const QSize want = requested.isValid() && !requested.isEmpty() ? requested : QSize(112, 44);
  Js& js = Js::instance();
  auto* canvas = new JsCanvas(want.width(), want.height());
  QJSValue ctx = js.engine().newQObject(canvas);
  canvas->setTransform(want.width() / 56.0, 0, 0, want.height() / 22.0, 0, 0);
  if (p.value(0) == "nib") js.call("samples.nibSample", {ctx, p.value(1), "#" + p.value(2), p.value(3).toDouble(), p.value(4) == "1"});
  else js.call("samples.smoothingSample", {ctx, p.value(1).toDouble(), "#" + p.value(2), p.value(3) == "1"});
  QImage img = canvas->image();
  if (size) *size = img.size();
  return img;
}
