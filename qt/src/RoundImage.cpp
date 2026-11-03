#include "RoundImage.h"

#include <QCache>
#include <QPainter>
#include <QPainterPath>

namespace {
/// Decoded pictures by source, so scrolling and re-showing the library
/// doesn't decode them again.
QCache<size_t, QImage>& cache() {
  static QCache<size_t, QImage> c(64 * 1024);  // cost in KB of pixels
  return c;
}

QImage decode(const QString& src) {
  if (src.isEmpty()) return {};
  const size_t key = qHash(src);
  if (QImage* hit = cache().object(key)) return *hit;
  QImage img;
  if (src.startsWith("data:")) {
    const int comma = src.indexOf(',');
    if (comma > 0) {
      const bool b64 = src.left(comma).endsWith(";base64");
      const QByteArray raw = src.mid(comma + 1).toLatin1();
      img.loadFromData(b64 ? QByteArray::fromBase64(raw) : QByteArray::fromPercentEncoding(raw));
    }
  } else {
    img.load(src.startsWith("qrc:") ? src.mid(3) : src);
  }
  if (!img.isNull()) cache().insert(key, new QImage(img), qMax<qsizetype>(1, img.sizeInBytes() / 1024));
  return img;
}

QPainterPath roundedBox(const QRectF& r, qreal tl, qreal tr, qreal br, qreal bl) {
  const qreal m = qMin(r.width(), r.height()) / 2;
  tl = qMin(tl, m), tr = qMin(tr, m), br = qMin(br, m), bl = qMin(bl, m);
  QPainterPath p;
  p.moveTo(r.left() + tl, r.top());
  p.lineTo(r.right() - tr, r.top());
  if (tr > 0) p.arcTo(r.right() - 2 * tr, r.top(), 2 * tr, 2 * tr, 90, -90);
  p.lineTo(r.right(), r.bottom() - br);
  if (br > 0) p.arcTo(r.right() - 2 * br, r.bottom() - 2 * br, 2 * br, 2 * br, 0, -90);
  p.lineTo(r.left() + bl, r.bottom());
  if (bl > 0) p.arcTo(r.left(), r.bottom() - 2 * bl, 2 * bl, 2 * bl, 270, -90);
  p.lineTo(r.left(), r.top() + tl);
  if (tl > 0) p.arcTo(r.left(), r.top(), 2 * tl, 2 * tl, 180, -90);
  p.closeSubpath();
  return p;
}
}  // namespace

RoundImage::RoundImage(QQuickItem* parent) : QQuickPaintedItem(parent) {
  setAntialiasing(true);
  connect(this, &RoundImage::changed, this, [this] { update(); });
}

void RoundImage::setSource(const QString& s) {
  if (s == m_source) return;
  m_source = s;
  m_image = decode(s);
  emit changed();
}

void RoundImage::paint(QPainter* p) {
  const QRectF box(0, 0, width(), height());
  const QPainterPath path = roundedBox(box, m_tl, m_tr, m_br, m_bl);
  p->setRenderHint(QPainter::Antialiasing);
  p->setRenderHint(QPainter::SmoothPixmapTransform);
  if (m_color.alpha() > 0) p->fillPath(path, m_color);
  if (m_image.isNull()) return;
  p->setClipPath(path);
  const QSizeF is = m_image.size();
  QRectF target = box;
  if (m_fit == "contain") {
    const qreal s = qMin(box.width() / is.width(), box.height() / is.height());
    const QSizeF sz = is * s;
    target = QRectF((box.width() - sz.width()) / 2, (box.height() - sz.height()) / 2, sz.width(), sz.height());
  } else {
    const qreal s = qMax(box.width() / is.width(), box.height() / is.height());
    const QSizeF sz = is * s;
    target = QRectF((box.width() - sz.width()) / 2, 0, sz.width(), sz.height());
  }
  p->drawImage(target, m_image);
}
