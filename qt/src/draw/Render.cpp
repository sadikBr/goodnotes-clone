#include "Render.h"

#include <QElapsedTimer>
#include <QHash>
#include <cmath>
#include <list>
#include <utility>

#include "../Core.h"
#include "../Canvas.h"
#include "Elements.h"
#include "Papers.h"

namespace draw {

double clockMs() {
  static QElapsedTimer t = [] {
    QElapsedTimer e;
    e.start();
    return e;
  }();
  return double(t.nsecsElapsed()) / 1e6;
}

QImage paperImage(const QString& tmpl, const QString& color, double w, double h, double scale, QRectF region) {
  static std::list<std::pair<QString, QImage>> cache;  // most recent first
  if (region.isEmpty()) region = QRectF(0, 0, w, h);
  // The paper depends only on these: pages alike share one picture (drawn once).
  const QString key = tmpl + QLatin1Char('|') + color + QStringLiteral("|%1|%2|%3|%4|%5|%6|%7").arg(w).arg(h).arg(scale).arg(region.x()).arg(region.y()).arg(region.width()).arg(region.height());
  for (auto it = cache.begin(); it != cache.end(); ++it)
    if (it->first == key) {
      QImage hit = it->second;
      cache.erase(it);
      cache.push_front({key, hit});
      return hit;
    }
  Canvas canvas(std::max(1, int(std::ceil(region.width() * scale))), std::max(1, int(std::ceil(region.height() * scale))));
  canvas.setTransform(scale, 0, 0, scale, -region.x() * scale, -region.y() * scale);
  drawPaper(canvas, tmpl, w, h, color);
  QImage out = canvas.image();
  cache.push_front({key, out});
  if (cache.size() > 10) cache.pop_back();
  return out;
}

QImage elementsImage(const QJsonArray& els, double scale, QRectF region) {
  Canvas canvas(std::max(1, int(std::ceil(region.width() * scale))), std::max(1, int(std::ceil(region.height() * scale))));
  canvas.setTransform(scale, 0, 0, scale, -region.x() * scale, -region.y() * scale);
  for (const QJsonValue& v : els) drawElement(canvas, v.toObject());
  return canvas.image();
}

const QImage* imageFor(const QString& src) {
  static QHash<QString, QImage> cache;
  if (auto it = cache.constFind(src); it != cache.constEnd()) return &*it;
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
  return &*cache.insert(src, img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
}

}  // namespace draw
