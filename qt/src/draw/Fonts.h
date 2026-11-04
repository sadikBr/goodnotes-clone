// The text font presets (was src/fonts.ts): their CSS font strings, as the
// canvas takes them, and right-to-left detection.
#pragma once

#include <QString>
#include <vector>

namespace draw {

struct FontPreset {
  QString id, label;
  /// CSS font-family list.
  QString family;
  /// The primary face.
  QString face;
  bool bold = false, italic = false;
};

const std::vector<FontPreset>& fontPresets();
/// The preset for `id` (earlier ids map to their replacement; unknown: Clean).
const FontPreset& fontPreset(const QString& id);
/// A family list with the Arabic face first (for right-to-left text).
QString arabicFirst(const QString& family);
/// CSS font string, e.g. `bold 24px "Inter", …` (`rtl`: the Arabic face first).
QString fontCss(const QString& id, double sizePx, bool bold = false, bool italic = false, bool rtl = false);
/// True when a paragraph reads right to left: its first strong letter is Arabic or Hebrew.
bool isRtl(const QString& text);
/// A number as JavaScript prints it in a template string (24, 12.5).
QString jsNumber(double v);

}  // namespace draw
