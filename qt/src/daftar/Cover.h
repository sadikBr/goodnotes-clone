// Cover pages (was src/cover.ts): placing the title and date for a design
// (ordinary text boxes, tagged so they can be laid out again when the design
// changes), applying a design to a notebook, and the cover's pictures.
#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <vector>

#include "../editor/Model.h"

namespace cover {

/// "November 2026": the date a new cover shows.
QString monthYear();
/// The title and date text boxes for a new cover of design `design`.
std::vector<ed::El> coverTextElements(const QString& design, const QString& title, const QString& date, double w, double h);
/// Draws and stores a cover page's thumbnail (the library shows it full-size).
void refreshCoverThumbnail(const QJsonObject& page);
/// Puts the title and this month on a new notebook's cover, and draws its thumbnail.
void setUpCover(const QJsonObject& nb);
/// Applies a design and colour: switches the cover page's design (moving its
/// title and date into place, keeping their text), or adds a cover page.
void applyCover(const QJsonObject& nb, const QString& design, const QString& color);
/// A notebook was renamed: the title on its cover (if it has one) follows.
void retitle(const QString& notebookId, const QString& oldTitle, const QString& title);
/// A picture of a cover design with the given title and colour, `pixelWidth` wide.
QImage coverPreview(const QString& design, const QString& color, const QString& title, int pixelWidth, bool landscape);

}  // namespace cover
