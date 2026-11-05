// The zoom box's view: a region of a page, drawn sharp at the box's size
// (paper, PDF and everything on it), with the stroke being written on it.
#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class InkCanvas;

class ZoomBoxView : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(InkCanvas* canvas READ canvas WRITE setCanvas NOTIFY changed)
  Q_PROPERTY(int page MEMBER m_page WRITE setPage NOTIFY changed)
  /// The region shown (page units).
  Q_PROPERTY(QRectF region MEMBER m_region WRITE setRegion NOTIFY changed)
public:
  explicit ZoomBoxView(QQuickItem* parent = nullptr);
  InkCanvas* canvas() const { return m_canvas; }
  void setCanvas(InkCanvas* c);
  void setPage(int p);
  void setRegion(const QRectF& r);
  void paint(QPainter* painter) override;

signals:
  void changed();

private:
  void stale();
  QPointer<InkCanvas> m_canvas;
  int m_page = -1;
  QRectF m_region;
  // The page under the stroke, as last drawn.
  QImage m_base;
  QRectF m_baseRegion;
  int m_basePage = -1;
  quint64 m_baseVersion = 0;
  double m_baseScale = 0;
};
