#include "TextFormat.h"

#include <QRegularExpression>
#include <QStringList>

namespace ed {
namespace {
const QString kSpace = QStringLiteral("[\\t\\n\\x{0B}\\f\\r \\x{A0}\\x{1680}\\x{2000}-\\x{200A}\\x{2028}\\x{2029}\\x{202F}\\x{205F}\\x{3000}\\x{FEFF}]");
const QRegularExpression& listMarker() {
  static const QRegularExpression re("^(•|◦|-|\\*|[0-9]{1,3}[.)])" + kSpace + "+");
  return re;
}
bool blank(const QString& s) {
  static const QRegularExpression re("^" + kSpace + "*$");
  return re.match(s).hasMatch();
}
QString strip(const QString& p) {
  const auto m = listMarker().match(p);
  return m.hasMatch() ? p.mid(m.capturedLength()) : p;
}
}  // namespace

QString listKindOf(const QString& p) {
  static const QRegularExpression bullet("^•" + kSpace + "+");
  static const QRegularExpression number("^[0-9]{1,3}[.)]" + kSpace + "+");
  if (bullet.match(p).hasMatch()) return "bullet";
  if (number.match(p).hasMatch()) return "number";
  return {};
}

QString currentList(const QString& text) {
  QStringList paras;
  for (const QString& p : text.split('\n'))
    if (!blank(p)) paras << p;
  if (paras.isEmpty()) return {};
  const QString first = listKindOf(paras[0]);
  if (first.isEmpty()) return {};
  for (const QString& p : paras)
    if (listKindOf(p) != first) return {};
  return first;
}

QString toggleList(const QString& text, const QString& kind) {
  QStringList paras = text.split('\n');
  if (blank(text)) return kind == "bullet" ? "• " : "1. ";
  if (currentList(text) == kind) {
    for (QString& p : paras) p = strip(p);
    return paras.join('\n');
  }
  int n = 0;
  for (QString& p : paras) {
    if (blank(p)) continue;
    const QString body = strip(p);
    p = kind == "bullet" ? "• " + body : QString("%1. %2").arg(++n).arg(body);
  }
  return paras.join('\n');
}

std::optional<Edit> continueList(const QString& value, int caret) {
  const int lineStart = caret > 0 ? value.lastIndexOf('\n', caret - 1) + 1 : 0;
  const int lineEndIdx = value.indexOf('\n', caret);
  const int lineEnd = lineEndIdx == -1 ? int(value.size()) : lineEndIdx;
  const QString line = value.mid(lineStart, lineEnd - lineStart);
  const auto m = listMarker().match(line);
  if (!m.hasMatch()) return std::nullopt;
  if (blank(line.mid(m.capturedLength()))) {
    // Empty item: remove the marker and leave the list.
    return Edit{value.left(lineStart) + value.mid(lineStart + m.capturedLength()), lineStart};
  }
  static const QRegularExpression num("^([0-9]{1,3})([.)])");
  const auto nm = num.match(m.captured(0));
  const QString marker = nm.hasMatch() ? QString("%1%2 ").arg(nm.captured(1).toInt() + 1).arg(nm.captured(2)) : m.captured(1) + " ";
  const QString insert = "\n" + marker;
  return Edit{value.left(caret) + insert + value.mid(caret), caret + int(insert.size())};
}

}  // namespace ed
