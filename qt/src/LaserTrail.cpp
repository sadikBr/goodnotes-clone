#include "LaserTrail.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <cmath>

#include "InkCanvas.h"

LaserTrail::LaserTrail(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  connect(this, &LaserTrail::changed, this, &QQuickItem::update);
}

void LaserTrail::setCanvas(InkCanvas* c) {
  if (c == m_canvas) return;
  if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
  m_canvas = c;
  if (c) connect(c, &InkCanvas::laserChanged, this, &QQuickItem::update);
  emit canvasChanged();
  update();
}

void LaserTrail::disc(QPointF c, std::vector<QPointF>& out) const {
  const int seg = m_radius > 4 ? 14 : 10;
  for (int k = 0; k < seg; ++k) {
    const qreal a0 = 2 * M_PI * k / seg, a1 = 2 * M_PI * (k + 1) / seg;
    out.insert(out.end(), {c, c + QPointF(std::cos(a0), std::sin(a0)) * m_radius, c + QPointF(std::cos(a1), std::sin(a1)) * m_radius});
  }
}

QSGNode* LaserTrail::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  auto* node = static_cast<QSGGeometryNode*>(old);
  if (!node) {
    node = new QSGGeometryNode;
    auto* g = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    g->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(g);
    node->setFlag(QSGNode::OwnsGeometry);
    auto* m = new QSGFlatColorMaterial;
    node->setMaterial(m);
    node->setFlag(QSGNode::OwnsMaterial);
  }
  static_cast<QSGFlatColorMaterial*>(node->material())->setColor(m_color);
  node->markDirty(QSGNode::DirtyMaterial);

  // The trail grows at its end: only new points get triangles. A quad per
  // segment; a disc at each end and where the line turns (round joins).
  static const std::vector<std::vector<QPointF>> none;
  const auto& runs = m_canvas ? m_canvas->laserRuns() : none;
  const bool shrank = runs.size() < m_runs || (runs.size() == m_runs && m_runs > 0 && runs[m_runs - 1].size() < m_points);
  if (shrank || m_builtRadius != m_radius) {
    m_tris.clear();
    m_runs = 0;
    m_points = 0;
    m_builtRadius = m_radius;
  }
  const qreal r = m_radius;
  for (size_t ri = m_runs ? m_runs - 1 : 0; ri < runs.size(); ++ri) {
    const auto& run = runs[ri];
    size_t from = ri + 1 == m_runs ? m_points : 0;
    if (from == 0 && !run.empty()) disc(run[0], m_tris);
    for (size_t i = std::max<size_t>(from, 1); i < run.size(); ++i) {
      const QPointF a = run[i - 1], c = run[i];
      const QPointF d = c - a;
      const qreal len = std::hypot(d.x(), d.y());
      if (len < 1e-6) continue;
      const QPointF nn = QPointF(-d.y(), d.x()) / len * r;
      m_tris.insert(m_tris.end(), {a - nn, c - nn, c + nn, a - nn, c + nn, a + nn});
      // A join where it turns (judged at a, once c is known).
      if (i >= 2) {
        const QPointF e = a - run[i - 2];
        const qreal le = std::hypot(e.x(), e.y());
        if (le > 1e-6 && (e.x() * d.x() + e.y() * d.y()) / (le * len) < 0.985) disc(a, m_tris);
      }
    }
    m_runs = ri + 1;
    m_points = run.size();
  }
  // Round ends (the last point moves on as the trail grows, so not stored).
  std::vector<QPointF> ends;
  for (const auto& run : runs)
    if (!run.empty()) disc(run.back(), ends);
  QSGGeometry* g = node->geometry();
  g->allocate(int(m_tris.size() + ends.size()));
  auto* v = g->vertexDataAsPoint2D();
  for (const QPointF& p : m_tris) (v++)->set(float(p.x()), float(p.y()));
  for (const QPointF& p : ends) (v++)->set(float(p.x()), float(p.y()));
  node->markDirty(QSGNode::DirtyGeometry);
  return node;
}
