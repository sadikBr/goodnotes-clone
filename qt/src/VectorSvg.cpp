#include "VectorSvg.h"

#include <QPainter>
#include <cmath>

VectorSvg::VectorSvg(QQuickItem* parent) : QQuickPaintedItem(parent) {
  setAntialiasing(true);
  setFillColor(Qt::transparent);
  connect(this, &VectorSvg::changed, this, [this] { update(); });
}

void VectorSvg::setSvg(const QString& s) {
  if (s == m_svg) return;
  m_svg = s;
  m_renderer.load(s.toUtf8());
  emit changed();
}

qreal VectorSvg::reach() const {
  qreal r = 0;
  for (const QPointF& c : {m_box.topLeft(), m_box.topRight(), m_box.bottomLeft(), m_box.bottomRight()}) r = std::max(r, std::hypot(c.x(), c.y()));
  return std::ceil(r * m_scale) + 2;
}

void VectorSvg::paint(QPainter* p) {
  if (!m_renderer.isValid() || m_box.isEmpty()) return;
  p->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
  p->translate(width() / 2, height() / 2);
  p->rotate(m_angle * 180 / M_PI);
  p->scale(m_scale, m_scale);
  m_renderer.render(p, m_box);
}
