#include "ZoomBoxView.h"

#include <QPainter>
#include <QQuickWindow>

#include "Canvas.h"
#include "InkCanvas.h"
#include "draw/Strokes.h"

ZoomBoxView::ZoomBoxView(QQuickItem* parent) : QQuickPaintedItem(parent) {
  setOpaquePainting(true);
  setAntialiasing(true);
}

void ZoomBoxView::setCanvas(InkCanvas* c) {
  if (c == m_canvas) return;
  if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
  m_canvas = c;
  if (c) {
    connect(c, &InkCanvas::historyChanged, this, &ZoomBoxView::stale);
    connect(c, &InkCanvas::pagesChanged, this, &ZoomBoxView::stale);
    connect(c, &InkCanvas::liveInkChanged, this, [this] { update(); });
  }
  stale();
  emit changed();
}

void ZoomBoxView::setPage(int p) {
  if (p == m_page) return;
  m_page = p;
  update();
  emit changed();
}

void ZoomBoxView::setRegion(const QRectF& r) {
  if (r == m_region) return;
  m_region = r;
  update();
  emit changed();
}

void ZoomBoxView::stale() {
  m_baseVersion = 0;
  m_basePage = -1;
  update();
}

void ZoomBoxView::paint(QPainter* painter) {
  painter->fillRect(boundingRect(), QColor("#fffdf7"));
  if (!m_canvas || m_page < 0 || m_region.isEmpty() || width() < 2) return;
  const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
  const double scale = width() / m_region.width() * dpr;
  const quint64 version = m_canvas->pageVersion(m_page);
  if (m_base.isNull() || m_basePage != m_page || m_baseRegion != m_region || std::abs(m_baseScale - scale) > 1e-6 || m_baseVersion != version) {
    m_base = m_canvas->regionImage(m_page, m_region, scale);
    m_basePage = m_page;
    m_baseRegion = m_region;
    m_baseScale = scale;
    m_baseVersion = version;
  }
  if (!m_base.isNull()) painter->drawImage(boundingRect(), m_base);
  // The stroke being written.
  const QJsonObject live = m_canvas->liveStroke(m_page);
  if (!live.isEmpty()) {
    const int w = int(std::ceil(width() * dpr)), h = int(std::ceil(height() * dpr));
    Canvas c(w, h);
    c.setTransform(scale, 0, 0, scale, -m_region.x() * scale, -m_region.y() * scale);
    draw::drawStroke(c, live);
    painter->drawImage(boundingRect(), c.image());
  }
}
