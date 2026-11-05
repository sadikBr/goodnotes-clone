// A notebook's contents (a PDF's own chapters, a daftar's classes and the
// user's headings, in page order) and the contents page that lists them:
// shared by the editor and the assistant.
#pragma once

#include <QJsonArray>
#include <QStringList>
#include <QVariantList>
#include <vector>

#include "editor/Model.h"

namespace contentspage {

/// The contents of a notebook from its stored pages ({ id, backgroundAsset,
/// backgroundPage, section }) and its headings ({ id, page (a page id), title, level }):
/// [{ kind ("pdf" | "section" | "heading"), title, level, page (index), id? }], in page order.
QVariantList contents(const QJsonArray& pages, const QJsonArray& headings);

/// The contents page's own elements (marked `contents`), for a page `w` × `h`
/// listing `entries` (from contents(), without the page itself); `pageIds` are
/// the notebook's pages, for the links.
std::vector<ed::El> pageElements(double w, double h, const QVariantList& entries, const QStringList& pageIds);

}  // namespace contentspage
