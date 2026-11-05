#include "FindText.h"

#include <algorithm>
#include <vector>

namespace findtext {
/// Text compared without case, and without Arabic short vowels or tatweel.
QString foldForFind(const QString& s) {
  QString out;
  out.reserve(s.size());
  for (const QChar c : s) {
    const ushort u = c.unicode();
    if ((u >= 0x064B && u <= 0x0652) || u == 0x0640 || u == 0x0670) continue;
    // Alef forms as plain alef.
    if (u == 0x0622 || u == 0x0623 || u == 0x0625) out += QChar(0x0627);
    else out += c.toCaseFolded();
  }
  return out;
}
/// Where `q` (folded) is in `text`, as an index into `text` itself (-1: not there), from `from`.
int findFolded(const QString& text, const QString& q, int from) {
  // Folding can drop characters: map folded positions back.
  QString folded;
  std::vector<int> back;
  for (int i = 0; i < text.size(); ++i) {
    const QString f = foldForFind(QString(text[i]));
    for (const QChar c : f) {
      folded += c;
      back.push_back(i);
    }
  }
  int start = 0;
  while (start < int(back.size()) && back[size_t(start)] < from) ++start;
  const int at = folded.indexOf(q, start);
  return at < 0 ? -1 : back[size_t(at)];
}
QVariantMap snippetAround(const QString& text, int at, int len) {
  const QString flat = QString(text).replace('\n', ' ');
  const int from = std::max(0, at - 30);
  const int to = std::min(int(flat.size()), at + len + 50);
  QString sn = flat.mid(from, to - from);
  int pos = at - from;
  if (from > 0) sn = "…" + sn, pos += 1;
  if (to < flat.size()) sn += "…";
  return {{"snippet", sn}, {"at", pos}, {"len", len}};
}
}  // namespace findtext
