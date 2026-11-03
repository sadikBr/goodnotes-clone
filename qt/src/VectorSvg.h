// An SVG drawn as vectors at the screen's resolution, rotated and scaled by
// the painter itself (not as a picture turned afterwards): the drawing
// instruments stay sharp at any angle and zoom.
#pragma once

#include <QQuickPaintedItem>
#include <QSvgRenderer>
#include <QtQml/qqmlregistration.h>

class VectorSvg : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString svg READ svg WRITE setSvg NOTIFY changed)
  /// The SVG's viewBox in its units, around the pivot (0, 0).
  Q_PROPERTY(QRectF box MEMBER m_box NOTIFY changed)
  /// Turn about the pivot, in radians.
  Q_PROPERTY(qreal angle MEMBER m_angle NOTIFY changed)
  /// Pixels per unit.
  Q_PROPERTY(qreal pixelsPerUnit MEMBER m_scale NOTIFY changed)
  /// How far the drawing reaches from the pivot, in pixels (the item is twice that, centred on it).
  Q_PROPERTY(qreal reach READ reach NOTIFY changed)
public:
  explicit VectorSvg(QQuickItem* parent = nullptr);
  QString svg() const { return m_svg; }
  void setSvg(const QString& s);
  qreal reach() const;
  void paint(QPainter* p) override;

signals:
  void changed();

private:
  QString m_svg;
  QSvgRenderer m_renderer;
  QRectF m_box;
  qreal m_angle = 0;
  qreal m_scale = 1;
};
