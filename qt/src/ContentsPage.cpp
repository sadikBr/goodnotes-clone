#include "ContentsPage.h"

#include <QHash>
#include <QJsonObject>
#include <QVariantMap>
#include <algorithm>

#include "Pdf.h"

namespace contentspage {

QVariantList contents(const QJsonArray& pages, const QJsonArray& headings) {
  struct E {
    int page, order;
    QVariantMap v;
  };
  std::vector<E> all;
  int order = 0;
  // The PDFs' own contents, on the pages that show them.
  QHash<QString, QHash<int, int>> shown;  // asset → its page → notebook page
  for (int i = 0; i < pages.size(); ++i) {
    const QJsonObject p = pages[i].toObject();
    const QString asset = p["backgroundAsset"].toString();
    if (!asset.isEmpty() && !p["backgroundPage"].isNull()) {
      auto& in = shown[asset];
      const int bp = p["backgroundPage"].toInt();
      if (!in.contains(bp)) in.insert(bp, i);
    }
  }
  for (auto it = shown.constBegin(); it != shown.constEnd(); ++it)
    for (const PdfFile::Entry& e : PdfDocs::instance().outline(it.key()))
      if (it->contains(e.page))
        all.push_back({it->value(e.page), order++, {{"kind", "pdf"}, {"title", e.title}, {"level", std::min(e.level, 3)}, {"page", it->value(e.page)}}});
  // A daftar's classes: where each one's pages start.
  QString section;
  for (int i = 0; i < pages.size(); ++i) {
    const QString sec = pages[i].toObject()["section"].toString();
    if (!sec.isEmpty() && sec != section) all.push_back({i, order++, {{"kind", "section"}, {"title", sec}, {"level", 0}, {"page", i}}});
    section = sec;
  }
  // The user's headings.
  QHash<QString, int> index;
  for (int i = 0; i < pages.size(); ++i) index.insert(pages[i].toObject()["id"].toString(), i);
  for (const QJsonValue& h : headings) {
    const QJsonObject m = h.toObject();
    const auto at = index.constFind(m["page"].toString());
    if (at == index.constEnd()) continue;  // its page was deleted
    all.push_back({*at, order++, {{"kind", "heading"}, {"id", m["id"].toString()}, {"title", m["title"].toString()}, {"level", std::clamp(m["level"].toInt(), 0, 3)}, {"page", *at}}});
  }
  std::stable_sort(all.begin(), all.end(), [](const E& a, const E& b) { return a.page != b.page ? a.page < b.page : a.order < b.order; });
  QVariantList out;
  for (const E& e : all) out.append(e.v);
  return out;
}

std::vector<ed::El> pageElements(double w, double h, const QVariantList& entries, const QStringList& pageIds) {
  const double margin = std::max(48.0, w * 0.09), width = w - 2 * margin;
  const double top = margin + 78;
  const int n = std::max(1, int(entries.size()));
  const double lineH = std::clamp((h - top - margin) / n, 22.0, 36.0);
  const int fits = int((h - top - margin) / lineH);
  const double fs = std::clamp(lineH * 0.48, 11.0, 16.0);
  std::vector<ed::El> add;
  auto text = [&](double x, double y, double tw, const QString& t, double size, const QString& color, bool bold, const QString& align, const QJsonObject& link) {
    ed::El el;
    el.id = ed::newId();
    el.kind = ed::Kind::Text;
    el.x = x;
    el.y = y;
    el.w = tw;
    el.text = t;
    el.fontSize = size;
    el.color = color;
    el.font = QStringLiteral("sans");
    if (bold) el.bold = true;
    if (align != "left") el.align = align;
    el.extra["contents"] = true;
    if (!link.isEmpty()) el.extra["link"] = link;
    add.push_back(std::move(el));
  };
  text(margin, margin, width, QStringLiteral("Contents"), 30, "#1d1f23", true, "left", {});
  if (entries.isEmpty())
    text(margin, top, width, QStringLiteral("Add headings in the pages panel (Contents), and they're listed here."), 14, "#6b7280", false, "left", {});
  for (int k = 0; k < std::min(int(entries.size()), fits); ++k) {
    const QVariantMap e = entries[k].toMap();
    const int level = std::clamp(e.value("level").toInt(), 0, 3);
    const int page = e.value("page").toInt();
    const double y = top + k * lineH, indent = level * 22;
    // A quiet link (a small arrow, not a chip with its name): the list says where it goes.
    const QJsonObject link{{"kind", "page"}, {"page", pageIds.value(page)}, {"compact", true}};
    text(margin + indent, y, width - indent - 64, e.value("title").toString(), level ? fs - 1 : fs, level ? "#374151" : "#1d1f23", level == 0, "left", link);
    text(margin + width - 56, y, 56, QString::number(page + 1), fs, "#6b7280", false, "right", link);
  }
  if (int(entries.size()) > fits)
    text(margin, top + fits * lineH, width, QStringLiteral("…and %1 more").arg(int(entries.size()) - fits), fs - 1, "#6b7280", false, "left", {});
  return add;
}

}  // namespace contentspage
