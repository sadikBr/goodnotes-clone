// Matching text for find in a notebook: without case, Arabic short vowels,
// tatweel or hamza on alef, so "الاستاذ" finds "الأستاذ".
#pragma once

#include <QString>
#include <QVariantMap>

namespace findtext {

QString foldForFind(const QString& s);
/// Where `q` (folded) is in `text`, as an index into `text` itself (-1: not there), from `from`.
int findFolded(const QString& text, const QString& q, int from = 0);
/// A short piece of `text` around a match at `at` (`len` long): { snippet, at, len }.
QVariantMap snippetAround(const QString& text, int at, int len);

}  // namespace findtext
