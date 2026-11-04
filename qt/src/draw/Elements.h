// Drawing page elements (was src/editor/render.ts): strokes, shapes, text
// boxes (word wrap, lists, alignment, Arabic), sticky notes and pictures, and
// whole pages for export. Elements are the editor's JSON objects.
#pragma once

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <memory>
#include <vector>

class Canvas;

namespace draw {

struct TextStyle {
  QString font;
  bool bold = false, italic = false;
  QString align;  ///< left (default), center, right
  static TextStyle of(const QJsonObject& el);
};

struct LaidLine {
  QString text;
  double x, y, width;
  /// The line belongs to a right-to-left (Arabic) paragraph.
  bool rtl;
  /// The canvas font it was measured with.
  QString font;
};

/// Lays out text: word wrap, list items with a hanging indent and
/// left/centre/right alignment. Measures on `c` (its font is changed).
std::vector<LaidLine> layoutText(Canvas& c, const QString& text, double x, double y, double w, double size, double lineH, const TextStyle& style);
/// The same, on a canvas kept for measuring.
std::vector<LaidLine> layoutText(const QString& text, double x, double y, double w, double size, double lineH, const TextStyle& style);
/// Where text laid out as a text box at (x, y), `w` wide, actually puts ink
/// (glyph outlines, tall letters and marks included); empty for no ink.
QRectF textInk(const QString& text, double x, double y, double w, double size, const TextStyle& style);
/// Text box height including word-wrapped lines.
double textHeight(const QString& text, double fontSize, double width, const TextStyle& style);
/// A text box's, sticky note's or picture's box (unrotated).
QRectF boxOf(const QJsonObject& el);
/// Width of the longest line of `text` in the given font and size.
double measureText(const QString& text, const QString& font, double size);
/// Width a text box needs to show every paragraph on one line.
double naturalTextWidth(const QJsonObject& el);

/// Draws one element.
void drawElement(Canvas& c, const QJsonObject& el);
/// Paper, an imported PDF page (if any) and the elements, onto `c` in page units.
void paintPage(Canvas& c, const QString& tmpl, const QString& paper, double w, double h, const QJsonArray& els, const QImage& background = {});
/// A whole page on a fresh canvas `scale` pixels per page unit.
std::unique_ptr<Canvas> renderPage(const QString& tmpl, const QString& paper, double w, double h, const QJsonArray& els, double scale, const QImage& background = {});

}  // namespace draw
