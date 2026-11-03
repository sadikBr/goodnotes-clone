// The page canvas: a notebook's pages, one under the other, drawn on the
// GPU through Qt Quick's scene graph, with pen, highlighter and eraser.
//
// Committed ink is a mesh per stroke, built once: scrolling and zooming
// only change a transform, so they cost the same however much ink there
// is. Elements this canvas can't draw yet are kept as they are and saved
// back unchanged.
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <memory>
#include <vector>

#include "Ink.h"

class QSGNode;

class InkCanvas : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString notebookId READ notebookId WRITE setNotebookId NOTIFY notebookIdChanged)
  Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
  Q_PROPERTY(QString penStyle READ penStyle WRITE setPenStyle NOTIFY penStyleChanged)
  Q_PROPERTY(QColor penColor READ penColor WRITE setPenColor NOTIFY penColorChanged)
  Q_PROPERTY(QColor highlighterColor READ highlighterColor WRITE setHighlighterColor NOTIFY highlighterColorChanged)
  Q_PROPERTY(qreal penWidth READ penWidth WRITE setPenWidth NOTIFY penWidthChanged)
  Q_PROPERTY(qreal highlighterWidth READ highlighterWidth WRITE setHighlighterWidth NOTIFY highlighterWidthChanged)
  Q_PROPERTY(qreal eraserWidth READ eraserWidth WRITE setEraserWidth NOTIFY eraserWidthChanged)
  Q_PROPERTY(qreal smoothing READ smoothing WRITE setSmoothing NOTIFY smoothingChanged)
  Q_PROPERTY(bool penOnly READ penOnly WRITE setPenOnly NOTIFY penOnlyChanged)
  Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
  Q_PROPERTY(int pageCount READ pageCount NOTIFY pagesChanged)
  Q_PROPERTY(int currentPage READ currentPage NOTIFY viewChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  Q_PROPERTY(int hiddenElements READ hiddenElements NOTIFY pagesChanged)

public:
  explicit InkCanvas(QQuickItem* parent = nullptr);
  ~InkCanvas() override;

  QString notebookId() const { return m_notebookId; }
  void setNotebookId(const QString& id);
  QString tool() const { return m_tool; }
  void setTool(const QString& t);
  QString penStyle() const { return ink::penName(m_penStyle); }
  void setPenStyle(const QString& s);
  QColor penColor() const { return m_penColor; }
  void setPenColor(const QColor& c);
  QColor highlighterColor() const { return m_hlColor; }
  void setHighlighterColor(const QColor& c);
  qreal penWidth() const { return m_penWidth; }
  void setPenWidth(qreal w);
  qreal highlighterWidth() const { return m_hlWidth; }
  void setHighlighterWidth(qreal w);
  qreal eraserWidth() const { return m_eraserWidth; }
  void setEraserWidth(qreal w);
  qreal smoothing() const { return m_smoothing; }
  void setSmoothing(qreal s);
  bool penOnly() const { return m_penOnly; }
  void setPenOnly(bool on);
  qreal zoom() const { return m_zoom; }
  int pageCount() const { return int(m_pages.size()); }
  int currentPage() const;
  bool canUndo() const { return !m_undo.empty(); }
  bool canRedo() const { return !m_redo.empty(); }
  int hiddenElements() const;

  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void zoomBy(qreal factor);
  Q_INVOKABLE void fitWidth();
  Q_INVOKABLE void goToPage(int index);
  Q_INVOKABLE void addPage();
  /// Saves pending changes now (before leaving the notebook).
  Q_INVOKABLE void flush();

signals:
  void notebookIdChanged();
  void toolChanged();
  void penStyleChanged();
  void penColorChanged();
  void highlighterColorChanged();
  void penWidthChanged();
  void highlighterWidthChanged();
  void eraserWidthChanged();
  void smoothingChanged();
  void penOnlyChanged();
  void viewChanged();
  void pagesChanged();
  void historyChanged();
  /// The pen was seen (pen-only mode turns on the first time).
  void penUsed();
  void error(const QString& message);

protected:
  QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
  void updatePolish() override;
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
  void itemChange(ItemChange change, const ItemChangeData& value) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void hoverMoveEvent(QHoverEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void touchEvent(QTouchEvent* e) override;

private:
  struct Stroke {
    QString id;
    int z = 0;
    QJsonObject data;  // as stored (other fields kept as they are)
    std::vector<ink::Point> pts;
    ink::Pen pen = ink::Pen::Fountain;
    QColor color;
    float width = 3;
    float smooth = ink::kSmoothNormal;
    QRectF bounds;
    std::shared_ptr<ink::Mesh> mesh;
    void build();
  };
  struct Page {
    QString id;
    float w = 794, h = 1123;
    QString tmpl = "blank";
    QColor paper = QColor("#fffdf7");
    float top = 0;  // layout: page origin y (page units)
    bool loaded = false;
    bool dirty = false;
    quint64 version = 1;  // bumped when its ink changes (the scene rebuilds it)
    std::vector<Stroke> strokes;
    QJsonArray others;  // elements not drawn here yet, kept as stored
  };
  /// One step of undo history.
  struct Change {
    int page;
    std::vector<Stroke> added, removed;
  };
  struct Gesture {
    enum Kind { None, Draw, Erase, Pan } kind = None;
    int page = -1;
    Stroke live;
    ink::SpeedInk speed;
    std::vector<Stroke> erased;
    QPointF lastScreen;
    int pointerId = 0;
  };

  void loadNotebook();
  void refresh();
  void ensureLoaded(int page);
  void layoutPages();
  void clampScroll();
  QPointF toPage(QPointF screen, int* pageIndex) const;
  int pageAt(QPointF screen) const;
  void begin(QPointF screen, float pressure, double tMs, bool stylus, bool eraserEnd);
  void extend(QPointF screen, float pressure, double tMs, bool stylus);
  void end();
  void eraseAt(int page, QPointF p);
  void record(Change c);
  void apply(const Change& c, bool reverse);
  void markDirty(int page);
  void saveNow();
  static QJsonObject storedOf(const Stroke& s);
  void zoomAt(qreal z, QPointF screenAnchor);
  bool isPenOnlyBlocked(bool stylus) const;

  QString m_notebookId;
  QString m_tool = "pen";
  ink::Pen m_penStyle = ink::Pen::Fountain;
  QColor m_penColor = QColor("#111827");
  QColor m_hlColor = QColor("#facc15");
  qreal m_penWidth = 3, m_hlWidth = 18, m_eraserWidth = 14;
  qreal m_smoothing = ink::kSmoothNormal;
  bool m_penOnly = false;

  std::vector<Page> m_pages;
  qreal m_zoom = 1;
  QPointF m_scroll;  // screen pixels
  float m_maxPageW = 794;
  float m_contentH = 0;  // page units
  bool m_fitPending = true;

  Gesture m_g;
  std::vector<Change> m_undo, m_redo;
  QTimer m_saveTimer;
  QPointer<QQuickWindow> m_window;
  QPointF m_hover{-1, -1};
  bool m_hoverOnPage = false;
  // Pinch zoom
  qreal m_pinchStartDist = 0, m_pinchStartZoom = 1;
  QPointF m_pinchMid;

  // Render-thread bookkeeping (touched only in updatePaintNode, while the GUI thread waits).
  struct PageNodeState {
    quint64 version = 0;
    QSGNode* node = nullptr;
  };
  std::vector<PageNodeState> m_nodeState;
  bool m_rebuildAll = true;
  bool m_liveChanged = false;
};
