// The editor for one notebook (was qt/js/editorCore.ts): tools, gestures,
// selection, undo, text editing and the overlay drawn over the active page,
// in page coordinates. The canvas (InkCanvas) draws the elements and sends
// pointer events; QML shows the tool and selection bars and the text editor.
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <optional>
#include <vector>

#include "Geometry.h"
#include "Instrument.h"
#include "Model.h"
#include "Snap.h"

class Canvas;

namespace ed {

using Els = std::vector<ElPtr>;

class EditorCore {
public:
  struct PageState {
    QJsonObject meta;  ///< id, width, height, template, paperColor, section, ...
    std::optional<Els> elements;
    bool dirty = false;
  };

  explicit EditorCore(const QJsonArray& pages);
  ~EditorCore();

  // Tool state (set by the app).
  QString tool = "pen";
  QHash<QString, QString> colors{{"pen", "#111827"}, {"highlighter", "#facc15"}, {"shape", "#111827"}, {"text", "#111827"}, {"sticky", "#fef08a"}, {"laser", "#ff1f1f"}};
  QHash<QString, double> widths{{"pen", 3}, {"highlighter", 18}, {"eraser", 14}, {"shape", 3}, {"text", 20}, {"sticky", 18}};
  QString penStyle = "fountain";
  /// Construction lines: "dash" or "dot", and the spacing (1 = normal).
  QString penDash = "dash";
  double penGap = 1;
  double streamline = 0.38;
  QString shapeKind = "auto";
  bool shapeFill = false;
  QString eraserMode = "precise";
  /// Scratching out ink with a pen scribble erases it (Gestures.h).
  bool scratchOut = true;
  /// How many scratch-outs so far (the canvas says so and fades the ink).
  int scratches = 0;
  /// What the last scratch-out took, drawn fading at this opacity (0: done).
  double fadeLevel = 0;
  void clearFade() { m_faded.clear(); fadeLevel = 0; ++overlayRev; }
  void setFade(double level) { fadeLevel = level; ++overlayRev; }
  QString textFont = "sans";
  struct TextFormat {
    bool bold = false, italic = false;
    QString align = "left";
  } textFormat;
  double zoom = 1;
  /// Width of the view in screen pixels (the ruler keeps its grips within reach).
  double viewWidth = 1000;

  /// Bumped whenever something the app shows changes.
  quint64 rev = 0, overlayRev = 0, selectionRev = 0, instrumentRev = 0;
  /// Called after a timer changed something (holding a stroke still).
  std::function<void()> onTimer;

  // ------------------------------------------------------------ pages
  std::vector<PageState> pages;
  int active = 0;
  const QJsonObject& page() const { return pages[size_t(active)].meta; }
  double pageWidth() const { return page()["width"].toDouble(794); }
  double pageHeight() const { return page()["height"].toDouble(1123); }
  /// The active page's elements (loaded when first asked for).
  Els& elements() { return load(active); }
  Els& load(int i);
  void activate(int i);
  void forgetAll();
  void setPages(const QJsonArray& pages);
  bool isHidden(int i, const QString& id) const { return i == active && m_hidden.contains(id); }
  /// An element's bounds, worked out once.
  static const Rect& boxOf(const El& el);
  /// Replaces the active page's elements from their JSON (the daftar's
  /// changes); unchanged ones are kept as they were.
  void setElementsJson(const QJsonArray& els);
  QVariantMap textFormatMap() const { return {{"bold", textFormat.bold}, {"italic", textFormat.italic}, {"align", textFormat.align}}; }

  // ------------------------------------------------------------ history and saving
  void undo();
  void redo();
  bool canUndo() const { return !m_undo.empty(); }
  bool canRedo() const { return !m_redo.empty(); }
  size_t undoDepth() const { return m_undo.size(); }
  size_t redoDepth() const { return m_redo.size(); }
  /// Goes up with each new change (not with undo or redo).
  quint64 newChanges = 0;
  void addElement(ElPtr el);
  bool dirty() const;
  /// Pages with unsaved changes (their flags are cleared: the app saves them).
  std::vector<int> takeDirty();
  void markDirty(int i);
  /// Before a change made from outside (the daftar): one undo step.
  void snapshot();
  /// After it: the page is saved and redrawn.
  void changed();

  // ------------------------------------------------------------ pointer
  /// A press on page `pi`. Returns "draw" (native live ink), "ruler" (ink along the instrument), "edit" (open the text editor),
  /// "cell" (type in a table's cell: takeCellRequest) or "".
  QString down(int pi, double x, double y, double p, double t, const QString& tool, bool shift);
  void move(double x, double y, double p, double t, bool shift, bool alt);
  void hover(double x, double y);
  QString cursorAt(double x, double y);
  void up();
  void cancel();
  bool liveHidden() const;
  struct LiveTransform {
    std::vector<QString> ids;
    Affine m;
  };
  std::optional<LiveTransform> liveTransform() const;
  bool hasGesture() const { return m_g.type != G::None; }

  // ------------------------------------------------------------ instruments
  QVariantMap instrument() const;
  void showInstrument(const QString& kind, double cx, double cy);
  void hideInstrument();
  void placeInstrument(double cx, double cy, double angle);
  void setArm(int i, double deg);
  bool instrumentContains(double x, double y) const { return m_ruler.contains(x, y); }

  // ------------------------------------------------------------ selection
  std::optional<Rect> selectionBounds();
  void clearSelection();
  void selectAll();
  QVariantMap selectionInfo();
  void styleSelectedText(const QVariantMap& change);
  void updateSelectedShape(const QString& change, double value);
  void deleteSelection();
  void recolorSelection(const QString& color);
  int copySelection();
  int cutSelection();
  int pasteClipboard();
  void duplicateSelection();
  void insertImage(const QString& src, double naturalW, double naturalH, double cx, double cy);
  /// A text box with `value` at (x, y) on the active page (`w` wide), selected.
  void insertText(const QString& value, double x, double y, double w);
  /// A graph ({ functions (one per line), xmin, xmax, ymin, ymax, grid }) in
  /// the box (x, y, w, h), selected.
  void insertGraph(const QVariantMap& spec, double x, double y, double w, double h);
  /// Changes the selected graph (one graph selected); false when there's none.
  bool updateSelectedGraph(const QVariantMap& spec);
  /// The selected picture's new box, the part of it shown ([x, y, w, h], 0..1) and its cut ("rect", "rounded", "circle").
  /// Links everything selected to `link` ({ kind: "page"|"notebook"|"url", … }); none: unlinks it.
  bool linkSelection(const QJsonObject& link);
  /// Page `page`'s links: where each is (linked things near each other together) and where it goes.
  std::vector<std::pair<Rect, QJsonObject>> linksOn(int page);
  /// Page `pi`'s elements that `drop` picks give way to `add` (not undoable: for
  /// pages the app writes itself, like the contents page).
  void replaceOnPage(int pi, const std::function<bool(const El&)>& drop, std::vector<El> add);
  /// Selects these elements on page `pi`.
  void selectIds(int pi, const QStringList& ids);
  /// The words written on page `pi` (pen strokes grouped into lines and words; only the
  /// selected ones if `selectionOnly`): each word's box and strokes, in reading order.
  struct InkWord {
    Rect box;
    QStringList ids;
  };
  std::vector<InkWord> inkWords(int pi, bool selectionOnly);
  /// The selected picture's src (one picture selected; else empty).
  QString selectedImageSrc();
  bool cropSelectedImage(double x, double y, double w, double h, const QVariantList& crop, const QString& mask);

  // ------------------------------------------------------------ tables
  /// An empty table, `rows` × `cols`, `w` wide, with its top left at (x, y), selected.
  void insertTable(int rows, int cols, double x, double y, double w);
  /// The selection's hand-drawn grid becomes a table (typed text in its cells
  /// goes into them; handwriting stays where it is). False with no grid.
  bool tableFromSelection();
  /// A table cell on the active page: { id, row, col, rows, cols, x, y, w, h
  /// (its box on the page), text, fontSize, font }; empty when there's none.
  QVariantMap tableCell(const QString& id, int row, int col);
  /// The cell at (x, y) on the active page (see tableCell).
  QVariantMap tableCellAt(double x, double y);
  /// Sets a cell's text (rows grow to fit it): one undo step.
  bool setTableCell(const QString& id, int row, int col, const QString& text);
  /// See changeTable (Table.h).
  bool changeTableById(const QString& id, const QString& op, int index);
  /// New column and row sizes (shares of the table's box).
  bool setTableSizes(const QString& id, const QVariantList& cols, const QVariantList& rows);
  /// The cell a text-tool press landed in ({ id, row, col }), taken once.
  QVariantMap takeCellRequest() { QVariantMap m = m_cellRequest; m_cellRequest.clear(); return m; }
  /// The selection as a reusable piece: its elements from the top left of
  /// its box ({ elements, width, height }); empty with nothing selected.
  QJsonObject selectionAsPiece();
  /// A piece's elements put with their top left at (x, y), selected.
  void insertPiece(const QJsonArray& els, double x, double y);
  /// `value` written to the right of the selection (under it when there's no
  /// room), in `font`, sized to it and in its ink's colour, selected.
  bool writeBeside(const QString& value, const QString& font);
  QVariantList selectedInkStrokes();
  /// The selected elements' text (typed text and notes), in order.
  QString selectedText();
  bool inkToText(const QString& text, const QString& font, bool keepInk);

  // ------------------------------------------------------------ text editing
  /// The text box or sticky note being typed into (hidden on the page meanwhile).
  const ElPtr& editing() const { return m_editing; }
  void updateEdit(const QString& text);
  void applyFormat(const QVariantMap& change);
  void finishTextEdit();

  // ------------------------------------------------------------ drawing
  /// Everything drawn over the page: the gesture in progress, the selection and its handles.
  void drawOverlay(Canvas& c, int pi);
  /// Where the overlay draws on the active page, or none.
  std::optional<Rect> overlayBounds();

private:
  enum class G { None, Draw, Erase, Lasso, Move, Shape, Handle, Transform };
  struct Held {
    QString mode;  ///< transform, move
    ElPtr base;
    P2 anchor, pivot, still;
  };
  struct Guided {
    EdgeGuide edge;
    double offset;
  };
  struct Gesture {
    G type = G::None;
    quint64 serial = 0;
    // draw
    QString tool;
    std::vector<Pt> points;
    std::vector<double> times;
    ElPtr preview;  ///< draw, shape, handle
    std::optional<Held> held;
    std::vector<QString> absorb;
    std::optional<Guided> ruler;
    // erase
    bool changed = false;
    std::optional<P2> last;
    // lasso
    std::vector<P2> lasso;
    // move
    P2 start{0, 0};
    Els original;  ///< move, handle (one), transform
    double dx = 0, dy = 0;
    bool snapping = false;
    std::vector<Anchor> anchors;
    std::vector<Target> targets;
    std::vector<Guide> guides;
    // handle
    QString handle;
    // transform
    Rect box;
    Els previews;
    Affine m;
    double sx = 1, sy = 1, angle = 0;
  };
  struct Step {
    int page;
    Els els;
  };
  struct Recent {
    QString id;
    double t;
    std::vector<P2> pts;
  };

  QSet<QString> m_selection;
  QSet<QString> m_hidden;
  Gesture m_g;
  quint64 m_serial = 0;
  std::optional<P2> m_cursor;
  bool m_shift = false;
  std::vector<Step> m_undo, m_redo;
  /// Copied elements, shared by every open page view (copy in one, paste in another).
  static std::vector<El>& clipboard();
  ElPtr m_editing;
  QVariantMap m_cellRequest;
  /// The ink the last scratch-out took (on the active page), while it fades.
  Els m_faded;
  bool scratchAway(const std::vector<Pt>& points);
  /// A tap on masking tape: shows what's under it, or covers it again.
  bool toggleTapeAt(double x, double y);
  /// A held shape as strokes of the current pen (for the pens whose look it keeps).
  std::vector<El> penStrokes(const El& shape) const;
  bool m_editingNew = false;
  Els m_editingBefore;
  std::vector<Recent> m_recentInk;
  Instrument m_ruler;
  std::optional<QString> m_readout;
  QTimer m_holdTimer, m_stillTimer;

  void touchOverlay() { ++overlayRev; }
  void armHold();
  void holdFired();
  void stillFired();
  ElPtr refine();
  struct Multi {
    ElPtr shape;
    std::vector<QString> absorb;
  };
  std::optional<Multi> multiStrokeSymbol();
  void setReadout(const std::optional<QString>& text);
  QString guidedReadout() const;
  std::pair<P2, P2> rulerExtent() const;
  El straightHighlight(const std::vector<P2>& line) const;
  P2 pivotFor(const El& el) const;
  void armModeToggle();
  void updateHeld(P2 p);
  void buildSnap(const Els& moving);
  ElPtr makeShapeEl(const Shape& shape);
  ElPtr shapeElement(const Shape& shape, const QString& colorOf) const;
  void eraseAlong(P2 p);
  void eraseAt(double x, double y);
  Els selectedList();
  ElPtr singleShape();
  ElPtr singleSegment();
  std::optional<Rect> transformBox();
  void placeCopies(const std::vector<El>& items);
  Els selectedInk();
  void startTextEdit(const ElPtr& el, bool isNew);
  std::vector<P2> transformOutline() const;
  /// An element as the group transform `g` leaves it.
  El transformed(const El& el, const Gesture& g) const;
  void drawHeldHint(Canvas& c, const El& el, const QString& mode) const;
  double nextZ() { return nextZOf(elements()); }
  static double nextZOf(const Els& els);
  void stopTimers();

  // The selection's elements and box, worked out once per change.
  struct SelCache {
    const Els* els = nullptr;
    size_t len = 0;
    quint64 rev = 0, selRev = 0;
    int active = -1, size = -1;
    Els list;
    std::optional<Rect> box;
  };
  std::optional<SelCache> m_selCache;
  const SelCache& selected();
};

/// Picks a font size so `text` covers roughly the same area as the handwriting it replaces.
struct Fit {
  double fontSize, width;
};
Fit fitTextToBox(const QString& text, const QString& font, const Rect& box);
/// Enter in the text editor: continues a list ({value, caret}, or empty).
QVariantMap continueListFor(const QString& text, int caret);
/// Drops points closer than `min` to the last kept one (the ends stay).
std::vector<Pt> thinPoints(const std::vector<Pt>& points, double min);

// The overlay's pieces (were in src/editor/render.ts).
void drawSelection(Canvas& c, const Rect& r, double zoom);
void drawGroupHandles(Canvas& c, const Rect& box, double zoom);
void drawTransformedBox(Canvas& c, const std::vector<P2>& corners, double zoom);
void drawHandles(Canvas& c, const El& el, double zoom);
void drawLasso(Canvas& c, const std::vector<P2>& pts, double zoom);
void drawGuides(Canvas& c, const std::vector<Guide>& guides, double zoom);

}  // namespace ed
