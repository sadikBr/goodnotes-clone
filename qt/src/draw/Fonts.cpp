#include "Fonts.h"

#include <QHash>
#include <QRegularExpression>
#include <cmath>

namespace draw {
namespace {
const QString AR = QStringLiteral("\"Noto Sans Arabic\"");
const QString SANS = QStringLiteral("\"Inter\", ") + AR + QStringLiteral(", \"Segoe UI\", system-ui, -apple-system, sans-serif");
// After a handwriting face: Arabic, then Inter for characters the face lacks.
const QString HAND = AR + QStringLiteral(", \"Inter\", cursive");
}  // namespace

const std::vector<FontPreset>& fontPresets() {
  static const std::vector<FontPreset> presets = {
      {"sans", "Clean", SANS, "Inter", true, true},
      {"book", "Book", "\"Lora\", " + AR + ", \"Inter\", serif", "Lora", true, true},
      {"playpen", "Friendly", "\"Playpen Sans\", " + HAND, "Playpen Sans", true, false},
      {"mynerve", "Natural", "\"Mynerve\", " + HAND, "Mynerve", false, false},
      {"school", "School print", "\"Andika\", " + HAND, "Andika", true, false},
      {"cursive-fr", "French cursive", "\"Playwrite FR Moderne\", " + HAND, "Playwrite FR Moderne", false, false},
      {"cursive-trad", "Classic cursive", "\"Playwrite FR Trad\", " + HAND, "Playwrite FR Trad", false, false},
      {"marker", "Marker", "\"Shantell Sans\", " + HAND, "Shantell Sans", true, false},
      {"chalk", "Chalkboard", "\"Schoolbell\", " + HAND, "Schoolbell", false, false},
      {"quick", "Quick notes", "\"Gochi Hand\", " + HAND, "Gochi Hand", false, false},
      {"neat", "Neat", "\"Handlee\", " + HAND, "Handlee", false, false},
  };
  return presets;
}

const FontPreset& fontPreset(const QString& id) {
  // Earlier presets (their fonts were replaced): each shows as the closest new one.
  static const QHash<QString, QString> legacy = {
      {"caveat", "quick"}, {"patrick-hand", "neat"}, {"kalam", "playpen"}, {"indie-flower", "playpen"},
      {"shadows", "neat"}, {"dancing", "cursive-trad"}, {"architect", "school"}, {"edu", "school"},
  };
  const QString key = legacy.value(id, id);
  for (const FontPreset& p : fontPresets())
    if (p.id == key) return p;
  return fontPresets().front();
}

QString arabicFirst(const QString& family) {
  QString rest = family;
  // Like String.replace with a string: the first occurrence only.
  const int at = rest.indexOf(", " + AR);
  if (at >= 0) rest.remove(at, AR.size() + 2);
  return AR + ", " + rest;
}

QString jsNumber(double v) {
  if (std::isfinite(v) && v == std::floor(v) && std::abs(v) < 1e15) return QString::number(qint64(v));
  // The shortest digits that read back as the same number, as JavaScript prints.
  for (int digits = 1; digits <= 17; ++digits) {
    const QString s = QString::number(v, 'g', digits);
    if (s.toDouble() == v) return s;
  }
  return QString::number(v, 'g', 17);
}

QString fontCss(const QString& id, double sizePx, bool bold, bool italic, bool rtl) {
  const QString family = fontPreset(id).family;
  return QString(italic ? "italic " : "") + (bold ? "bold " : "") + jsNumber(sizePx) + "px " + (rtl ? arabicFirst(family) : family);
}

bool isRtl(const QString& text) {
  static const QRegularExpression strong(QStringLiteral("[\\x{0590}-\\x{08FF}\\x{FB1D}-\\x{FDFF}\\x{FE70}-\\x{FEFF}A-Za-z\\x{00C0}-\\x{024F}\\x{0370}-\\x{03FF}\\x{0400}-\\x{04FF}]"));
  static const QRegularExpression rtlChar(QStringLiteral("[\\x{0590}-\\x{08FF}\\x{FB1D}-\\x{FDFF}\\x{FE70}-\\x{FEFF}]"));
  const auto m = strong.match(text);
  return m.hasMatch() && rtlChar.match(m.captured(0)).hasMatch();
}

}  // namespace draw
