#include "Fonts.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>

namespace draw {
namespace {
const QString AR = QStringLiteral("\"Noto Sans Arabic\"");
const QString SANS = QStringLiteral("\"Inter\", ") + AR + QStringLiteral(", \"Segoe UI\", system-ui, -apple-system, sans-serif");
/// A handwriting face, then its Arabic partner (so Arabic has the same spirit
/// in each preset rather than one face for all), then Noto Sans Arabic and
/// Inter for characters they lack.
QString hand(const QString& face, const QString& arabic) {
  return "\"" + face + "\", \"" + arabic + "\", " + AR + QStringLiteral(", \"Inter\", cursive");
}
/// Every preset's Arabic face (for putting it first in right-to-left text).
const QStringList& arabicFaces() {
  static const QStringList faces = {"Noto Sans Arabic", "Noto Naskh Arabic", "Lemonada", "Katibeh", "Baloo Bhaijaan 2", "Aref Ruqaa", "Amiri", "Rakkas", "Changa", "Marhey", "El Messiri"};
  return faces;
}
}  // namespace

const std::vector<FontPreset>& fontPresets() {
  static const std::vector<FontPreset> presets = {
      {"sans", "Clean", SANS, "Inter", true, true},
      {"book", "Book", "\"Lora\", \"Noto Naskh Arabic\", " + AR + ", \"Inter\", serif", "Lora", true, true},
      {"playpen", "Friendly", hand("Playpen Sans", "Lemonada"), "Playpen Sans", true, false},
      {"mynerve", "Natural", hand("Mynerve", "Katibeh"), "Mynerve", false, false},
      {"school", "School print", hand("Andika", "Baloo Bhaijaan 2"), "Andika", true, false},
      {"cursive-fr", "French cursive", hand("Playwrite FR Moderne", "Aref Ruqaa"), "Playwrite FR Moderne", false, false},
      {"cursive-trad", "Classic cursive", hand("Playwrite FR Trad", "Amiri"), "Playwrite FR Trad", false, false},
      {"marker", "Marker", hand("Shantell Sans", "Rakkas"), "Shantell Sans", true, false},
      {"chalk", "Chalkboard", hand("Schoolbell", "Changa"), "Schoolbell", false, false},
      {"quick", "Quick notes", hand("Gochi Hand", "Marhey"), "Gochi Hand", false, false},
      {"neat", "Neat", hand("Handlee", "El Messiri"), "Handlee", false, false},
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
  // The list's first Arabic face moves to the front (its first mention only).
  int best = -1;
  QString face;
  for (const QString& f : arabicFaces()) {
    const int at = family.indexOf("\"" + f + "\"");
    if (at >= 0 && (best < 0 || at < best)) best = at, face = "\"" + f + "\"";
  }
  if (best < 0) return AR + ", " + family;
  QString rest = family;
  if (best >= 2 && rest.mid(best - 2, 2) == ", ") rest.remove(best - 2, face.size() + 2);
  else rest.remove(best, face.size() + (rest.mid(best + face.size(), 2) == ", " ? 2 : 0));
  return face + ", " + rest;
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
