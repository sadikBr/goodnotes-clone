// Paper templates (was src/editor/papers.ts): the lines, dots and layouts of
// every paper style, computed from the page size, in colours that suit the
// paper. Covers and daftar pages are drawn elsewhere.
#pragma once

#include <QString>
#include <vector>

class JsCanvas;

namespace draw {

/// Inkleaf units per millimetre (units are 1/96 inch).
constexpr double MM = 96 / 25.4;

/// True for dark paper, where lines and dots need to be light.
bool isDarkPaper(const QString& color);

struct PaperLayout {
  std::vector<double> rules;  ///< writing lines (y)
  double ruleFrom = 0, ruleTo = 0;  ///< their horizontal span
  bool hasMargin = false;
  double margin = 0;
};
/// Where the paper's writing lines and margin are (shared with alignment guides).
PaperLayout paperLayout(const QString& tmpl, double w, double h);

/// Draws the paper colour and the template's lines. False for a template
/// this doesn't draw (covers, daftar pages): only the paper colour is drawn.
bool drawPaper(JsCanvas& c, const QString& tmpl, double w, double h, const QString& paper);

}  // namespace draw
