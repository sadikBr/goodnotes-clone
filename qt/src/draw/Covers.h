// Notebook cover designs (was src/editor/covers.ts). Each design draws a
// whole cover page in the notebook's colour and says where its title and
// date go (they are ordinary, editable text boxes). "Random" patterns use a
// fixed seed, so a cover always looks the same.
#pragma once

#include <QString>
#include <vector>

class Canvas;

namespace draw {

struct CoverDesign {
  QString id, label, tmpl;
};
const std::vector<CoverDesign>& coverDesigns();
bool isCoverTemplate(const QString& t);
/// The design id of a cover template ("classic" when unknown).
QString coverDesignOf(const QString& t);
/// The template of a design id.
QString coverTemplate(const QString& design);

struct Box {
  double x = 0, y = 0, w = 0, h = 0;
};
/// Where a design puts its title and date, and how they look.
struct CoverText {
  Box title;
  Box date;  ///< x, y, w (h unused)
  QString align, titleColor, dateColor, font;
  double maxSize = 0, dateSize = 0;
};
CoverText coverText(const QString& design, double w, double h);
/// The cream panel in the middle of the zellige cover (the daftar's details go on it).
Box zelligePanel(double w, double h);

/// Mixes a #rrggbb colour with black (amount < 0) or white (amount > 0).
QString shade(const QString& color, double amount);

/// Draws a cover page (the page's paper colour is the notebook's cover colour).
void drawCoverDesign(Canvas& c, const QString& tmpl, double w, double h, const QString& color);

}  // namespace draw
