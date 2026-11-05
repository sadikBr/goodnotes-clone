#include "PdfOutline.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <functional>

namespace pdfoutline {
namespace {

/// The dictionary of object `num` (between "num 0 obj" and "endobj"), or empty.
QByteArray objectBody(const QByteArray& pdf, int num) {
  const QRegularExpression head(QStringLiteral("(?:^|[\\r\\n])%1 0 obj").arg(num));
  // The last one wins (a file may already have updates).
  qsizetype at = -1;
  for (auto it = head.globalMatch(QString::fromLatin1(pdf)); it.hasNext();) at = it.next().capturedEnd();
  if (at < 0) return {};
  const qsizetype end = pdf.indexOf("endobj", at);
  return end < 0 ? QByteArray() : pdf.mid(at, end - at).trimmed();
}

int refIn(const QByteArray& dict, const char* key) {
  const QRegularExpression re(QStringLiteral("/%1\\s+(\\d+)\\s+0\\s+R").arg(QLatin1String(key)));
  const auto m = re.match(QString::fromLatin1(dict));
  return m.hasMatch() ? m.captured(1).toInt() : -1;
}

/// The page objects in order, walking the pages tree from `node`.
void collectPages(const QByteArray& pdf, int node, std::vector<int>& out, int depth = 0) {
  if (depth > 32) return;
  const QByteArray body = objectBody(pdf, node);
  if (body.isEmpty()) return;
  if (!body.contains("/Kids")) {
    out.push_back(node);
    return;
  }
  const qsizetype k = body.indexOf("/Kids");
  const qsizetype open = body.indexOf('[', k), close = body.indexOf(']', open);
  if (open < 0 || close < 0) return;
  const QString kids = QString::fromLatin1(body.mid(open + 1, close - open - 1));
  static const QRegularExpression ref(QStringLiteral("(\\d+)\\s+0\\s+R"));
  for (auto it = ref.globalMatch(kids); it.hasNext();) collectPages(pdf, it.next().captured(1).toInt(), out, depth + 1);
}

/// A PDF text string for any language: UTF-16BE in hex, with its byte-order mark.
QByteArray textString(const QString& s) {
  QByteArray hex = "<FEFF";
  for (const QChar c : s) hex += QByteArray::number(c.unicode(), 16).rightJustified(4, '0').toUpper();
  return hex + ">";
}

}  // namespace

bool add(const QString& path, const std::vector<Entry>& entries) {
  if (entries.empty()) return true;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  const QByteArray pdf = f.readAll();
  f.close();
  // The trailer: its catalog, size and where the xref is.
  const qsizetype tr = pdf.lastIndexOf("trailer");
  const qsizetype sx = pdf.lastIndexOf("startxref");
  if (tr < 0 || sx < 0) return false;
  const QByteArray trailer = pdf.mid(tr, sx - tr);
  const int root = refIn(trailer, "Root");
  const QRegularExpression sizeRe(QStringLiteral("/Size\\s+(\\d+)"));
  const auto sm = sizeRe.match(QString::fromLatin1(trailer));
  bool okX = false;
  const qint64 prevXref = pdf.mid(sx + 9).trimmed().split('\n').value(0).trimmed().toLongLong(&okX);
  if (root < 0 || !sm.hasMatch() || !okX) return false;
  const int size = sm.captured(1).toInt();
  const int info = refIn(trailer, "Info");
  const QByteArray catalog = objectBody(pdf, root);
  const int pagesRoot = refIn(catalog, "Pages");
  if (catalog.isEmpty() || pagesRoot < 0) return false;
  std::vector<int> pages;
  collectPages(pdf, pagesRoot, pages);
  if (pages.empty()) return false;

  // The tree: each entry's parent is the last one before it at a lower level.
  struct Node {
    int obj = 0, parent = -1, page = 0;
    QString title;
    std::vector<int> kids;
  };
  std::vector<Node> nodes;
  std::vector<int> top, stack, levels;
  for (const Entry& e : entries) {
    if (e.page < 0 || e.page >= int(pages.size()) || e.title.trimmed().isEmpty()) continue;
    while (!stack.empty() && levels[stack.back()] >= e.level) stack.pop_back();
    Node n;
    n.title = e.title.trimmed().left(300);
    n.page = pages[e.page];
    n.parent = stack.empty() ? -1 : stack.back();
    const int i = int(nodes.size());
    nodes.push_back(n);
    levels.push_back(e.level);
    if (n.parent < 0) top.push_back(i);
    else nodes[n.parent].kids.push_back(i);
    stack.push_back(i);
  }
  if (nodes.empty()) return true;
  const int outlines = size;
  for (int i = 0; i < int(nodes.size()); ++i) nodes[i].obj = size + 1 + i;
  const int newSize = size + 1 + int(nodes.size());
  std::function<int(int)> count = [&](int i) { int c = 0; for (int k : nodes[i].kids) c += 1 + count(k); return c; };

  QByteArray out = pdf;
  if (!out.endsWith('\n')) out += '\n';
  QHash<int, qint64> offsets;
  auto obj = [&](int num, const QByteArray& body) {
    offsets[num] = out.size();
    out += QByteArray::number(num) + " 0 obj\n" + body + "\nendobj\n";
  };
  auto siblings = [&](const std::vector<int>& list, int at, QByteArray& d) {
    if (at > 0) d += " /Prev " + QByteArray::number(nodes[list[at - 1]].obj) + " 0 R";
    if (at + 1 < int(list.size())) d += " /Next " + QByteArray::number(nodes[list[at + 1]].obj) + " 0 R";
  };
  int total = 0;
  for (int t : top) total += 1 + count(t);
  obj(outlines, "<< /Type /Outlines /First " + QByteArray::number(nodes[top.front()].obj) + " 0 R /Last " + QByteArray::number(nodes[top.back()].obj) +
                    " 0 R /Count " + QByteArray::number(total) + " >>");
  for (int i = 0; i < int(nodes.size()); ++i) {
    const Node& n = nodes[i];
    const std::vector<int>& list = n.parent < 0 ? top : nodes[n.parent].kids;
    const int at = int(std::find(list.begin(), list.end(), i) - list.begin());
    QByteArray d = "<< /Title " + textString(n.title) + " /Parent " + QByteArray::number(n.parent < 0 ? outlines : nodes[n.parent].obj) + " 0 R";
    siblings(list, at, d);
    if (!n.kids.empty())
      d += " /First " + QByteArray::number(nodes[n.kids.front()].obj) + " 0 R /Last " + QByteArray::number(nodes[n.kids.back()].obj) + " 0 R /Count " + QByteArray::number(count(i));
    // At the top of its page (the page's height, from its MediaBox).
    double top = 842;
    {
      static const QRegularExpression box(QStringLiteral("/MediaBox\\s*\\[\\s*[-\\d.]+\\s+[-\\d.]+\\s+[-\\d.]+\\s+([-\\d.]+)"));
      const auto m = box.match(QString::fromLatin1(objectBody(pdf, n.page)));
      if (m.hasMatch()) top = m.captured(1).toDouble();
    }
    d += " /Dest [" + QByteArray::number(n.page) + " 0 R /XYZ 0 " + QByteArray::number(top, 'f', 2) + " 0] >>";
    obj(n.obj, d);
  }
  // The catalog again, pointing at the outline, and opening with it shown.
  QByteArray cat = catalog;
  static const QRegularExpression oldKeys(QStringLiteral("/(Outlines\\s+\\d+\\s+0\\s+R|PageMode\\s*/\\w+)"));
  cat = QString::fromLatin1(cat).remove(oldKeys).toLatin1();
  const qsizetype close = cat.lastIndexOf(">>");
  if (close < 0) return false;
  cat.insert(close, " /Outlines " + QByteArray::number(outlines) + " 0 R /PageMode /UseOutlines ");
  obj(root, cat);

  // The xref for what's new, then the trailer.
  const qint64 xref = out.size();
  out += "xref\n";
  out += QByteArray::number(root) + " 1\n" + QByteArray::number(offsets[root]).rightJustified(10, '0') + " 00000 n \n";
  out += QByteArray::number(outlines) + " " + QByteArray::number(newSize - outlines) + "\n";
  for (int num = outlines; num < newSize; ++num) out += QByteArray::number(offsets[num]).rightJustified(10, '0') + " 00000 n \n";
  out += "trailer\n<< /Size " + QByteArray::number(newSize) + " /Root " + QByteArray::number(root) + " 0 R";
  if (info >= 0) out += " /Info " + QByteArray::number(info) + " 0 R";
  out += " /Prev " + QByteArray::number(prevXref) + " >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";

  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  return f.write(out) == out.size();
}

}  // namespace pdfoutline
