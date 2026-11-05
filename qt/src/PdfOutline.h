// Chapters (an outline, the bookmarks a PDF reader shows beside the pages)
// for a PDF that Qt wrote, which can't make them itself: added at the end of
// the file as an incremental update (new objects, a new catalog, a new xref).
#pragma once

#include <QString>
#include <vector>

namespace pdfoutline {

struct Entry {
  QString title;
  int level = 0;  ///< 0: a chapter; 1, 2…: inside the one above it
  int page = 0;   ///< among the file's pages, from 0
};

/// Adds `entries` (in reading order) to the PDF at `path`; false if it can't
/// be read the way Qt writes it (the file is then left as it was).
bool add(const QString& path, const std::vector<Entry>& entries);

}  // namespace pdfoutline
