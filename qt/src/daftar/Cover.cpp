// Cover pages (was src/cover.ts); see Cover.h.
#include "Cover.h"

#include <QLocale>
#include <QRegularExpression>
#include <cmath>

#include "../Canvas.h"
#include "../draw/Covers.h"
#include "../draw/Elements.h"
#include "Daftar.h"
#include "Store.h"

namespace cover {

namespace {

/// The title's and date's look and place for a design: the largest title size that fits on at most two lines.
struct Placed {
  ed::El title, date;
};
Placed layoutText(const QString& design, const QString& title, const QString& date, double w, double h) {
  const draw::CoverText t = draw::coverText(design, w, h);
  const draw::Box& box = t.title;
  double size = t.maxSize;
  auto width = [&](double s) { return draw::measureText(title, t.font, s) * 1.06; };
  while (size > 16 && width(size) > box.w * (size > t.maxSize * 0.78 ? 1 : 2)) size -= 2;
  const int lines = width(size) > box.w ? 2 : 1;
  const double lineH = size * 1.35;
  const double y = std::max(box.y - 6, box.y + (box.h - lines * lineH) / 2);
  Placed p;
  for (ed::El* e : {&p.title, &p.date}) {
    e->kind = ed::Kind::Text;
    e->font = t.font;
    e->align = t.align;
    e->rotation = 0;
  }
  p.title.x = box.x;
  p.title.y = y;
  p.title.w = box.w;
  p.title.fontSize = size;
  p.title.color = t.titleColor;
  p.title.bold = t.font == "sans";
  p.title.text = title;
  p.date.x = t.date.x;
  p.date.y = t.date.y;
  p.date.w = t.date.w;
  p.date.fontSize = t.dateSize;
  p.date.color = t.dateColor;
  p.date.bold = false;
  p.date.text = date;
  return p;
}

/// Puts a placed title or date onto an existing text box (its other keys kept).
void place(ed::El& e, const ed::El& from, const QString& role) {
  e.font = from.font;
  e.align = from.align;
  e.rotation = from.rotation;
  e.x = from.x;
  e.y = from.y;
  e.w = from.w;
  e.fontSize = from.fontSize;
  e.color = from.color;
  e.bold = from.bold;
  e.text = from.text;
  e.role = role;
}

QJsonArray json(const std::vector<ed::El>& els) {
  QJsonArray a;
  for (const ed::El& e : els) a.append(e.toJson());
  return a;
}

}  // namespace

QString monthYear() { return QLocale().toString(daftar::today(), "MMMM yyyy"); }

std::vector<ed::El> coverTextElements(const QString& design, const QString& title, const QString& date, double w, double h) {
  Placed l = layoutText(design, title, date, w, h);
  l.title.id = ed::newId();
  l.title.z = 0;
  l.title.role = "cover-title";
  l.date.id = ed::newId();
  l.date.z = 1;
  l.date.role = "cover-date";
  return {l.title, l.date};
}

void refreshCoverThumbnail(const QJsonObject& page) {
  const std::vector<ed::El> els = store::elements(page["id"].toString());
  const double w = page["width"].toDouble(), h = page["height"].toDouble();
  const double scale = 420 / w;  // sharp: the library shows covers full-size
  const auto c = draw::renderPage(page["template"].toString(), page["paperColor"].toString(), w, h, json(els), scale);
  store::setPageThumbnail(page["id"].toString(), c->toDataURL("image/jpeg", 0.85));
}

void setUpCover(const QJsonObject& nb) {
  const QJsonArray pages = store::pages(nb["id"].toString());
  if (pages.isEmpty()) return;
  const QJsonObject page = pages.at(0).toObject();
  if (!draw::isCoverTemplate(page["template"].toString())) return;
  store::saveElements(page["id"].toString(), coverTextElements(draw::coverDesignOf(page["template"].toString()), nb["title"].toString(), monthYear(), page["width"].toDouble(), page["height"].toDouble()));
  refreshCoverThumbnail(page);
}

void applyCover(const QJsonObject& nb, const QString& design, const QString& color) {
  const QString id = nb["id"].toString();
  store::setNotebookCover(id, color);
  const QString tmpl = draw::coverTemplate(design);
  const QJsonArray pages = store::pages(id);
  QJsonObject page = pages.isEmpty() ? QJsonObject() : pages.at(0).toObject();
  if (page.isEmpty() || !draw::isCoverTemplate(page["template"].toString())) {
    page = store::addPage(id, 0, tmpl);
    store::setPageTemplate(page["id"].toString(), tmpl, color);
    store::saveElements(page["id"].toString(), coverTextElements(design, nb["title"].toString(), monthYear(), page["width"].toDouble(), page["height"].toDouble()));
  } else {
    store::setPageTemplate(page["id"].toString(), tmpl, color);
    std::vector<ed::El> els = store::elements(page["id"].toString());
    // The title and date: tagged ones, or (older covers) the text matching them.
    static const QRegularExpression DATE_RE(QStringLiteral("^[\\p{L} .,-]*[0-9]{4}$"), QRegularExpression::UseUnicodePropertiesOption);
    int titleAt = -1, dateAt = -1;
    for (int i = 0; i < int(els.size()) && titleAt < 0; ++i) {
      const ed::El& e = els[size_t(i)];
      if (e.kind == ed::Kind::Text && (e.role == QStringLiteral("cover-title") || (!e.role && e.text == nb["title"].toString()))) titleAt = i;
    }
    for (int i = 0; i < int(els.size()) && dateAt < 0; ++i) {
      const ed::El& e = els[size_t(i)];
      if (i != titleAt && e.kind == ed::Kind::Text && (e.role == QStringLiteral("cover-date") || (!e.role && DATE_RE.match(e.text.trimmed()).hasMatch()))) dateAt = i;
    }
    const Placed l = layoutText(design, titleAt >= 0 ? els[size_t(titleAt)].text : nb["title"].toString(), dateAt >= 0 ? els[size_t(dateAt)].text : monthYear(), page["width"].toDouble(), page["height"].toDouble());
    if (titleAt >= 0) place(els[size_t(titleAt)], l.title, "cover-title");
    if (dateAt >= 0) place(els[size_t(dateAt)], l.date, "cover-date");
    store::saveElements(page["id"].toString(), els);
  }
  refreshCoverThumbnail(store::pages(id).at(0).toObject());
}

QImage coverPreview(const QString& design, const QString& color, const QString& title, int pixelWidth, bool landscape) {
  const double w = landscape ? 1123 : 794;
  const double h = landscape ? 794 : 1123;
  const std::vector<ed::El> els = coverTextElements(design, title.isEmpty() ? QStringLiteral("Untitled") : title, monthYear(), w, h);
  return draw::renderPage(draw::coverTemplate(design), color, w, h, json(els), pixelWidth / w)->image();
}

}  // namespace cover
