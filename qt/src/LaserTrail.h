// One layer of the laser pointer's trail (src/editor/laser.ts): the trail
// so far as round-ended tubes of one width and colour, on the GPU. The glow,
// tube and core are three of these, each in a layer of its own opacity
// (Editor.qml), so crossings never darken and long trails stay fast.
#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <vector>

class InkCanvas;

class LaserTrail : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(InkCanvas* canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
  /// Half the tube's width, in pixels.
  Q_PROPERTY(qreal radius MEMBER m_radius NOTIFY changed)
  Q_PROPERTY(QColor color MEMBER m_color NOTIFY changed)
public:
  explicit LaserTrail(QQuickItem* parent = nullptr);
  InkCanvas* canvas() const { return m_canvas; }
  void setCanvas(InkCanvas* c);

signals:
  void canvasChanged();
  void changed();

protected:
  QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
  QPointer<InkCanvas> m_canvas;
  // Triangles of the trail built so far (grown as it's drawn).
  std::vector<QPointF> m_tris;
  size_t m_runs = 0;     // runs (strokes) started in m_tris
  size_t m_points = 0;   // points of the last of them in m_tris
  qreal m_builtRadius = -1;
  void disc(QPointF c, std::vector<QPointF>& out) const;
  qreal m_radius = 3;
  QColor m_color = Qt::red;
};
