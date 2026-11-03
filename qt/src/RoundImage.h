// A picture (a data: URL, as the library stores thumbnails) in a box with a
// radius per corner, like CSS border-radius + object-fit. Also a plain
// rounded box when there's no picture.
#pragma once

#include <QColor>
#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class RoundImage : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString source READ source WRITE setSource NOTIFY changed)
  Q_PROPERTY(QColor color MEMBER m_color NOTIFY changed)
  /// "cover" (fill, cropped, anchored at the top) or "contain".
  Q_PROPERTY(QString fit MEMBER m_fit NOTIFY changed)
  Q_PROPERTY(qreal radiusTopLeft MEMBER m_tl NOTIFY changed)
  Q_PROPERTY(qreal radiusTopRight MEMBER m_tr NOTIFY changed)
  Q_PROPERTY(qreal radiusBottomRight MEMBER m_br NOTIFY changed)
  Q_PROPERTY(qreal radiusBottomLeft MEMBER m_bl NOTIFY changed)
  Q_PROPERTY(bool hasImage READ hasImage NOTIFY changed)
public:
  explicit RoundImage(QQuickItem* parent = nullptr);
  void paint(QPainter* p) override;
  QString source() const { return m_source; }
  void setSource(const QString& s);
  bool hasImage() const { return !m_image.isNull(); }
signals:
  void changed();

private:
  QString m_source;
  QImage m_image;
  QColor m_color = Qt::transparent;
  QString m_fit = "cover";
  qreal m_tl = 0, m_tr = 0, m_br = 0, m_bl = 0;
};
