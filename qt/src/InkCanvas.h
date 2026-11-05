// The notebook's pages, drawn on the GPU (Qt Quick scene graph), with the
// editor's tools. Editing itself (gestures, selection, undo) is the editor
// (editor/EditorCore); this item handles input, the view (scrolling, zoom),
// saving, and drawing:
//  - paper (draw/Papers, Covers, DaftarPapers), as a picture per page (sharper
//    ones made as the zoom settles; only the visible part at high zoom),
//  - pen and highlighter ink as triangle meshes built once per stroke,
//  - text, shapes, sticky notes, pictures and textured nibs (pencil,
//    calligraphy) as pictures (draw/Elements),
//  - the stroke being written as a live mesh, the selection and gesture
//    overlay (EditorCore::drawOverlay), the eraser ring and the laser.
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Ink.h"
#include "editor/EditorCore.h"

class QSGNode;
class QSGTexture;

class InkCanvas : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString notebookId READ notebookId WRITE setNotebookId NOTIFY notebookIdChanged)
  Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
  Q_PROPERTY(QString penStyle READ penStyle WRITE setPenStyle NOTIFY toolStateChanged)
  /// Construction lines: "dash" or "dot", and how far apart (1 = normal).
  Q_PROPERTY(QString penDash READ penDash WRITE setPenDash NOTIFY toolStateChanged)
  Q_PROPERTY(double penGap READ penGap WRITE setPenGap NOTIFY toolStateChanged)
  Q_PROPERTY(QVariantMap colors READ colors WRITE setColors NOTIFY toolStateChanged)
  Q_PROPERTY(QVariantMap widths READ widths WRITE setWidths NOTIFY toolStateChanged)
  Q_PROPERTY(QString shapeKind READ shapeKind WRITE setShapeKind NOTIFY toolStateChanged)
  Q_PROPERTY(bool shapeFill READ shapeFill WRITE setShapeFill NOTIFY toolStateChanged)
  Q_PROPERTY(QString eraserMode READ eraserMode WRITE setEraserMode NOTIFY toolStateChanged)
  Q_PROPERTY(QString textFont READ textFont WRITE setTextFont NOTIFY toolStateChanged)
  Q_PROPERTY(QString laserMode MEMBER m_laserMode NOTIFY toolStateChanged)
  /// The laser pointer's position (x < 0: none) and how far the trail has faded.
  Q_PROPERTY(QPointF laserDot READ laserDot NOTIFY laserChanged)
  Q_PROPERTY(qreal laserColorAlpha READ laserColorAlpha NOTIFY laserChanged)
  Q_PROPERTY(qreal laserCoreAlpha READ laserCoreAlpha NOTIFY laserChanged)
  Q_PROPERTY(QColor laserColor READ laserColor NOTIFY laserChanged)
  Q_PROPERTY(bool laserTrail READ laserTrail NOTIFY laserChanged)
  Q_PROPERTY(qreal smoothing READ smoothing WRITE setSmoothing NOTIFY toolStateChanged)
  Q_PROPERTY(bool penOnly READ penOnly WRITE setPenOnly NOTIFY penOnlyChanged)
  /// While the (unpinned) tool drawer is open a press on the page only closes it.
  Q_PROPERTY(bool dismissOnPress MEMBER m_dismissOnPress NOTIFY dismissOnPressChanged)
  // Settings screen: "light" | "normal" | "firm" (how hard to press for a full line).
  Q_PROPERTY(QString pressureCurve MEMBER m_pressureCurve NOTIFY settingsChanged)
  Q_PROPERTY(bool eraserEnd MEMBER m_eraserEndOn NOTIFY settingsChanged)
  // What the pen's side button does while held: "eraser" | "lasso" | "none".
  Q_PROPERTY(QString sideButton MEMBER m_sideButton NOTIFY settingsChanged)
  Q_PROPERTY(bool palmRejection MEMBER m_palmRejection NOTIFY settingsChanged)
  /// Scratching out ink with the pen erases it.
  Q_PROPERTY(bool scratchOut MEMBER m_scratchOut NOTIFY settingsChanged)
  /// Two fingers tapped together: undo; three: redo.
  Q_PROPERTY(bool fingerTaps MEMBER m_fingerTaps NOTIFY settingsChanged)
  /// A whiteboard: the page grows to the right and down as writing nears its edges.
  Q_PROPERTY(bool growable MEMBER m_growable NOTIFY settingsChanged)
  /// View only: every gesture scrolls (a second view of the notebook being edited).
  Q_PROPERTY(bool readOnly MEMBER m_readOnly NOTIFY settingsChanged)
  // The cursor for drawing tools: "cross" | "dot" | "none".
  Q_PROPERTY(QString penCursor READ penCursor WRITE setPenCursor NOTIFY settingsChanged)

  Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
  /// The pointer over the canvas (-1, -1 when away), and whether it's erasing now (eraser tool or the pen's eraser end).
  Q_PROPERTY(QPointF pointer READ pointer NOTIFY pointerChanged)
  Q_PROPERTY(bool rubbing READ rubbing NOTIFY pointerChanged)
  /// Writing, erasing or selecting on the page right now (the zoom bar
  /// fades meanwhile).
  Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)
  Q_PROPERTY(int pageCount READ pageCount NOTIFY pagesChanged)
  Q_PROPERTY(int currentPage READ currentPage NOTIFY viewChanged)
  Q_PROPERTY(int activePage READ activePage NOTIFY selectionChanged)
  /// Pages: [{ id, width, height, template, paperColor, thumbnail, favourite, section }].
  Q_PROPERTY(QVariantList pages READ pages NOTIFY pagesChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  /// What's selected ({ count, box, color, texts, font, bold, … }); see editorCore selectionInfo().
  Q_PROPERTY(QVariantMap selection READ selection NOTIFY selectionChanged)
  /// The text box or note being typed into (its element), or empty.
  Q_PROPERTY(QVariantMap editing READ editing NOTIFY editingChanged)
  /// Bold, italic and alignment for the next text box (the core's textFormat).
  Q_PROPERTY(QVariantMap textFormat READ textFormat NOTIFY toolStateChanged)
  /// The drawing instrument (ruler, protractor, set squares): see editorCore instrument().
  Q_PROPERTY(QVariantMap instrument READ instrument NOTIFY instrumentChanged)

public:
  explicit InkCanvas(QQuickItem* parent = nullptr);
  ~InkCanvas() override;

  QString notebookId() const { return m_notebookId; }
  void setNotebookId(const QString& id);
  QString tool() const { return m_tool; }
  void setTool(const QString& t);
  QString penStyle() const { return m_penStyleName; }
  void setPenStyle(const QString& s);
  QString penDash() const { return m_penDash; }
  void setPenDash(const QString& s);
  double penGap() const { return m_penGap; }
  void setPenGap(double g);
  QVariantMap colors() const { return m_colors; }
  void setColors(const QVariantMap& c);
  QVariantMap widths() const { return m_widths; }
  void setWidths(const QVariantMap& w);
  QString shapeKind() const { return m_shapeKind; }
  void setShapeKind(const QString& k);
  bool shapeFill() const { return m_shapeFill; }
  void setShapeFill(bool f);
  QString eraserMode() const { return m_eraserMode; }
  void setEraserMode(const QString& m);
  QString textFont() const { return m_textFont; }
  void setTextFont(const QString& f);
  qreal smoothing() const { return m_smoothing; }
  void setSmoothing(qreal s);
  bool penOnly() const { return m_penOnly; }
  void setPenOnly(bool on);
  QString penCursor() const { return m_penCursor; }
  void setPenCursor(const QString& c);
  qreal zoom() const { return m_zoom; }
  QPointF pointer() const { return m_hover; }
  bool writing() const { return m_writing; }
  bool rubbing() const { return m_rubbing; }
  int pageCount() const { return int(m_pages.size()); }
  int currentPage() const;
  int activePage() const;
  QVariantList pages() const;
  bool canUndo() const;
  bool canRedo() const;
  QVariantMap selection() const { return m_selection; }
  QVariantMap editing() const { return m_editing; }
  QVariantMap instrument() const { return m_instrument; }
  /// Shows `kind` in the middle of the view ("" hides it; the same kind again hides it).
  Q_INVOKABLE void showInstrument(const QString& kind);
  /// Moves and turns the instrument (page units, radians).
  Q_INVOKABLE void placeInstrument(qreal cx, qreal cy, qreal angle);
  Q_INVOKABLE void setInstrumentArm(int arm, qreal degrees);
  Q_INVOKABLE bool instrumentContains(qreal x, qreal y);
  /// A point in this item's coordinates as page units on the active page.
  Q_INVOKABLE QPointF itemToPage(QPointF p) const;

  // View
  Q_INVOKABLE void zoomBy(qreal factor);
  Q_INVOKABLE void setZoomLevel(qreal z);
  Q_INVOKABLE void fitWidth();
  Q_INVOKABLE void goToPage(int index);
  /// A page-units rectangle on page `page` in this item's coordinates.
  Q_INVOKABLE QRectF pageRectToItem(int page, const QRectF& r) const;

  // Editing
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void deleteSelection();
  Q_INVOKABLE void duplicateSelection();
  Q_INVOKABLE int copySelection();
  Q_INVOKABLE int cutSelection();
  Q_INVOKABLE int paste();
  Q_INVOKABLE void selectAll();
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE void recolorSelection(const QString& color);
  /// { font?, scale?, bold?, italic?, align?, list? }
  Q_INVOKABLE void styleSelection(const QVariantMap& change);
  /// "width" (with value), "fill", or a shape kind to convert to.
  Q_INVOKABLE void changeShape(const QString& change, qreal value = 0);
  /// Turns the selected handwriting into text in a handwriting font.
  /// The selected pen strokes ([[x, y], …] each), for handwriting recognition.
  Q_INVOKABLE QVariantList selectedInkStrokes();
  /// The selection's part of the page (paper, PDF and all) as a PNG data: URL
  /// about `maxSide` pixels on its long side: for the AI. "" with no selection.
  Q_INVOKABLE QString selectionImage(int maxSide = 1400);
  /// The selection's text and the plain text of handwriting-free elements ("" if none).
  Q_INVOKABLE QString selectionText();
  /// Adds `text` as a text box: under the selection, else in the middle of the view.
  Q_INVOKABLE void insertText(const QString& text);
  /// A graph ({ functions, xmin, xmax, ymin, ymax, grid }): beside the
  /// selection, else in the middle of the view.
  Q_INVOKABLE void insertGraph(const QVariantMap& spec);
  /// Changes the selected graph.
  Q_INVOKABLE bool updateGraph(const QVariantMap& spec);
  /// An empty table (rows × cols, the first a header) in the middle of the view, selected.
  Q_INVOKABLE void insertTable(int rows, int cols);
  /// The selected hand-drawn grid becomes a table; false when there's none.
  Q_INVOKABLE bool tableFromSelection();
  /// A table cell on the active page (EditorCore::tableCell) plus where it is
  /// in this item ("rect"); empty when there's none.
  Q_INVOKABLE QVariantMap tableCell(const QString& id, int row, int col);
  Q_INVOKABLE bool setTableCell(const QString& id, int row, int col, const QString& text);
  /// "addRow", "addCol", "removeRow", "removeCol" (at `index`, -1: the end) or "header".
  Q_INVOKABLE bool changeTable(const QString& id, const QString& op, int index = -1);
  /// A selected table's column and row edges in this item ({ xs, ys }; empty if none).
  Q_INVOKABLE QVariantMap tableEdges(const QString& id);
  Q_INVOKABLE bool setTableSizes(const QString& id, const QVariantList& cols, const QVariantList& rows);
  /// The selection as a reusable piece: { elements (JSON text), width,
  /// height, preview (PNG data: URL about `maxSide` on its long side) }.
  Q_INVOKABLE QVariantMap selectionPiece(int maxSide = 240);
  /// A piece's elements (JSON text) put in the middle of the view, selected.
  Q_INVOKABLE void insertPiece(const QString& elements, double width, double height);
  /// Writes `text` beside the selection in `font` (an answer next to a sum).
  Q_INVOKABLE bool writeBeside(const QString& text, const QString& font);
  /// Replaces the selected handwriting with `text` in `font` (keeping the ink if asked).
  Q_INVOKABLE bool applyFontToInk(const QString& text, const QString& font, bool keepInk);
  Q_INVOKABLE void insertImage(const QString& fileUrl);

  // Text editor
  Q_INVOKABLE void updateEdit(const QString& text);
  Q_INVOKABLE QVariantMap continueList(const QString& text, int caret);
  /// { bold?, italic?, align?, list?, font?, fontSize?, color? } for new text and the text being edited.
  Q_INVOKABLE void editFormat(const QVariantMap& change);
  Q_INVOKABLE void finishEdit();

  // Pages
  Q_INVOKABLE void addPage(int index = -1, const QString& tmpl = QString());
  Q_INVOKABLE void duplicatePage(int index);
  Q_INVOKABLE void deletePage(int index);
  Q_INVOKABLE void movePage(int index, int to);
  Q_INVOKABLE void togglePageFavourite(int index);
  /// Paper style and/or colour for one page or all of them.
  Q_INVOKABLE void setPaper(int index, const QString& tmpl, const QString& color, bool allPages);
  /// Reloads pages from the library (after changes made elsewhere).
  Q_INVOKABLE void reloadPages(bool elementsToo = false);
  /// Saves pending changes now (before leaving the notebook).
  Q_INVOKABLE void flush();
  QPointF laserDot() const { return m_laserDot; }
  qreal laserColorAlpha() const;
  qreal laserCoreAlpha() const;
  QColor laserColor() const;
  bool laserTrail() const { return !m_laser.empty(); }
  /// The trail so far, in item coordinates (one list per stroke).
  const std::vector<std::vector<QPointF>>& laserRuns() const { return m_laser; }
  QVariantMap textFormat() { return m_ed ? m_ed->textFormatMap() : QVariantMap(); }
  /**
   * Runs Inkleaf.daftarQt.<fn>(core, ...args) for the daftar bar and does
   * what it asks: reload the pages, go to a page (and a line on it).
   */
  Q_INVOKABLE QVariantMap daftar(const QString& fn, const QVariantList& args = {});
  /** Inkleaf.daftarQt.<fn>(core, ...args) without side effects (queries). */
  Q_INVOKABLE QVariant daftarQuery(const QString& fn, const QVariantList& args = {});
  /// A page's picture (paper and everything on it) at `scale`, as a data: URL.
  Q_INVOKABLE QString pagePreview(int index, qreal scale);
  /// A page drawn for export: { dataUrl, widthPx, heightPx, pageWidth, pageHeight }.
  Q_INVOKABLE QVariantMap exportPage(int index, qreal scale, bool jpeg);
  /** Writes these pages as a vector PDF at `path`; returns an error ("" when done). */
  Q_INVOKABLE QString exportPdf(const QString& path, const QVariantList& indexes, const QString& title);

signals:
  void pointerChanged();
  void notebookIdChanged();
  void toolChanged();
  void toolStateChanged();
  void penOnlyChanged();
  void settingsChanged();
  void viewChanged();
  void pagesChanged();
  void historyChanged();
  void selectionChanged();
  void editingChanged();
  void instrumentChanged();
  /// The pen was seen (pen-only mode turns on the first time).
  void penUsed();
  void message(const QString& text, bool error);
  void dismissOnPressChanged();
  void laserChanged();
  /// A press that only closed the drawer (see dismissOnPress).
  void dismissed();
  /// Ink was scratched out with the pen (Undo brings it back).
  void scratchedOut();
  /// Two or three fingers tapped: undone or redone.
  void fingerTapped(bool redo);
  /// The text tool tapped a table: type in this cell ({ id, row, col }).
  void cellEditRequested(const QVariantMap& cell);
  void writingChanged();

protected:
  QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
  void updatePolish() override;
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
  void itemChange(ItemChange change, const ItemChangeData& value) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void trackFingerTap(QTouchEvent* e);
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void hoverMoveEvent(QHoverEvent* e) override;
  void hoverLeaveEvent(QHoverEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void touchEvent(QTouchEvent* e) override;

private:
  /// One element as drawn: a mesh (pen ink) or a picture (everything else).
  struct Item {
    QString id;
    double rev = 0;      // the element's revision in the editor core (a new one when it changes)
    QJsonObject data;    // pictures: the element, for drawing it again at another scale
    bool mesh = false;
    std::shared_ptr<ink::Mesh> tris;
    QColor color;
    QImage image;  // pictures: drawn at `scale`, covering `rect` (page units)
    QRectF rect;
    double scale = 0;
    bool imageDirty = false;  // the render thread makes a new texture
    QSGTexture* texture = nullptr;
    QSGNode* node = nullptr;  // transform node → mesh or image node
  };
  struct Page {
    QString id;
    QJsonObject meta;
    float w = 794, h = 1123;
    QString tmpl = "blank";
    QString paper = "#fffdf7";
    float top = 0;  // layout: page origin y (page units)
    bool synced = false;
    quint64 version = 1;  // bumped when its items change
    std::vector<std::shared_ptr<Item>> items;  // in z order
    std::unordered_map<QString, std::shared_ptr<Item>> byId;
    // Paper pictures: the whole page, and a sharper part at high zoom.
    QImage paperImg, detailImg;
    QRectF detailRect;
    double paperScale = 0, detailScale = 0;
    bool paperDirty = false, detailDirty = false;
  };
  /// A gesture as the canvas sees it (the editor core has the details).
  struct Gesture {
    enum Kind { None, Core, Draw, Pan, Laser } kind = None;
    int page = -1;
    std::vector<ink::Point> pts;  // live ink, page units
    ink::SpeedInk speed;
    ink::Pen pen = ink::Pen::Fountain;
    int dashed = 0;  // construction lines: 1 dashes, 2 dots
    QColor color;
    float width = 3;
    std::shared_ptr<ink::Mesh> mesh;
    QPointF lastScreen;
    bool touch = false;
    bool stylus = false;
  };

  // The editor
  void pushToolState();
  void afterCore();  // syncs whatever the core changed
  void syncPage(int i);
  void syncSelection();
  void syncEditing();

  // Pages and view
  void loadNotebook();
  void applyPages(const QJsonArray& pages);
  void layoutPages();
  void clampScroll();
  QPointF origin() const;
  QPointF toPage(QPointF screen, int* pageIndex) const;
  QPointF toPageOf(int page, QPointF screen) const;
  int pageAt(QPointF screen) const;
  /// The page within `within` screen pixels of `s` (the closest), or -1.
  int pageNear(QPointF s, double within) const;
  void zoomAt(qreal z, QPointF screenAnchor);
  void refresh();
  double renderScale() const;
  void renderPictures(bool missingOnly = false);
  bool prefetchStep();
  /// The page's paper picture, with its imported PDF page on top (if any).
  /// A page's elements as JSON (for drawing them in C++).
  QJsonArray sceneJson(int index);
  QImage paperWithBackground(const Page& p, double scale, QRectF region);
  /// The library picture of page `i` (paper and items as drawn), stored with the page.
  void storeThumbnail(int i);  // papers and element pictures for what's in view

  // Input
  void press(QPointF screen, float pressure, bool stylus, const QString& toolOverride, bool touch, Qt::KeyboardModifiers mods);
  void drag(QPointF screen, float pressure, bool stylus, Qt::KeyboardModifiers mods);
  void release();
  float curved(qreal pressure) const;
  void updateCursor();
  bool navigatesOnly(bool stylus) const;

  QString m_notebookId;
  QString m_tool = "pen";
  QString m_penStyleName = "fountain";
  QString m_penDash = "dash";
  double m_penGap = 1;
  QVariantMap m_colors, m_widths;
  QString m_shapeKind = "auto";
  bool m_shapeFill = false;
  QString m_eraserMode = "precise";
  QString m_textFont = "sans";
  QString m_laserMode = "trail";
  qreal m_smoothing = ink::kSmoothNormal;
  bool m_penOnly = false;
  QString m_pressureCurve = "normal";
  bool m_eraserEndOn = true;
  QString m_sideButton = "eraser";
  bool m_palmRejection = true;
  bool m_scratchOut = true;
  bool m_fingerTaps = true;
  bool m_growable = false;
  void growIfNeeded();
  /// Scratch-outs seen, and the fading of what the last one took.
  int m_scratches = 0;
  double m_fadeStart = 0;
  QTimer m_fadeTick;
  /// A tap with several fingers (undo, redo): when it began, how many
  /// fingers, where each came down, and whether any moved.
  struct FingerTap {
    double start = 0;
    int fingers = 0;
    bool moved = false;
    QHash<int, QPointF> at;
  } m_tap;
  QString m_penCursor = "cross";
  double m_lastPenMs = -1e9;
  /// A pen press seen by the window, waiting for Qt Quick to say where it
  /// goes: if it reaches this item (as the mouse press Qt makes from it),
  /// it starts a stroke with this pressure; if a menu, button or panel on
  /// top takes it, nothing happens here.
  struct PendingPen {
    QPointF local;
    float pressure = 0.5f;
    QString override;
    Qt::KeyboardModifiers mods;
    double at = -1e9;
  } m_pendingPen;
  /// Pen moves for the eraser, lasso, moving and resizing come far more
  /// often than the screen shows them (unlike the mouse's, Qt doesn't merge
  /// them), and each erases or moves things on the page: the latest is kept
  /// and done once, on the next turn of the event loop. (Strokes keep every
  /// point.)
  struct PendingMove {
    QPointF local;
    float pressure = 0.5f;
    Qt::KeyboardModifiers mods;
    bool waiting = false;
  } m_pendingMove;
  void flushPendingMove();
  bool m_writing = false;
  void setWriting(bool on) {
    if (on == m_writing) return;
    m_writing = on;
    emit writingChanged();
  }
  QVariantMap m_instrument;
  double m_instrumentRev = -1;

  std::unique_ptr<ed::EditorCore> m_ed;  // the editor for the open notebook
  double m_coreRev = -1, m_overlayRev = -1, m_selectionRev = -1;
  QVariantMap m_selection, m_editing;

  std::vector<Page> m_pages;
  qreal m_zoom = 1;
  QPointF m_scroll;  // screen pixels
  float m_maxPageW = 794;
  float m_contentH = 0;  // page units
  bool m_fitPending = true;
  QTimer m_settle;  // the zoom stopped changing: sharper pictures
  QTimer m_saveTimer;
  QTimer m_thumbFill;  // missing page thumbnails
  QTimer m_prefetch;  // idle: pages further off loaded and drawn ahead of scrolling
  QTimer m_scrollGlide;  // a wheel notch's eased scroll
  double m_glideLast = 0;
  QPointF m_glideTarget;
  double m_lastWheel = -1e9, m_wheelBoost = 1, m_wheelNotches = 0;
  bool m_dismissOnPress = false;  // missing PDF page thumbnails
  QTimer m_laserTick;

  Gesture m_g;
  QPointer<QQuickWindow> m_window;
  QPointF m_hover{-1, -1};
  bool m_rubbing = false;
  bool m_readOnly = false;
  void setHover(QPointF p);
  qreal m_pinchStartDist = 0, m_pinchStartZoom = 1;
  QPointF m_pinchMid;

  // Overlay (selection, gesture), drawn by the core on the active page.
  QImage m_overlayImg;
  QRectF m_overlayRect;  // page units
  int m_overlayPage = -1;
  bool m_overlayDirty = false;
  // Live transform of selected items while they're dragged.
  QHash<QString, QTransform> m_liveTransform;
  bool m_liveTransformDirty = false;
  bool m_liveChanged = false;
  bool m_liveHidden = false;

  // Laser: recent points (item coordinates) with their times.
  std::vector<std::vector<QPointF>> m_laser;  // the trail, one list per stroke (item coordinates)
  double m_laserUp = -1;
  QPointF m_laserDot{-1, -1};

  // Render-thread bookkeeping (touched only in updatePaintNode, while the GUI thread waits).
  struct PageNodes {
    quint64 version = 0;
    QSGNode* page = nullptr;   // transform node (page origin)
    QSGNode* paper = nullptr;  // image node
    QSGNode* detail = nullptr; // image node (sharper part)
    QSGTexture* paperTex = nullptr;
    QSGTexture* detailTex = nullptr;
    QSGNode* items = nullptr;  // container of item nodes
  };
  std::vector<PageNodes> m_nodes;
  bool m_rebuildAll = true;
  std::vector<std::shared_ptr<Item>> m_graveyard;  // items whose nodes go next frame
  QSGTexture* m_overlayTex = nullptr;
};
