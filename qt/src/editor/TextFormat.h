// List formatting for text boxes (was src/editor/textFormat.ts). Lists are
// stored as plain-text markers ("• " and "1. "); the renderer adds a
// hanging indent for wrapped lines.
#pragma once

#include <QString>
#include <optional>

namespace ed {

/// "bullet", "number", or empty.
QString listKindOf(const QString& paragraph);
/// The list kind shared by every non-empty paragraph, or empty.
QString currentList(const QString& text);
/// Turns a list on for every non-empty paragraph (renumbering), or off when
/// the whole text already is that kind of list.
QString toggleList(const QString& text, const QString& kind);
struct Edit {
  QString value;
  int caret;
};
/// Enter inside a list: the next item, or the end of the list on an empty
/// item. None when not in a list.
std::optional<Edit> continueList(const QString& value, int caret);

}  // namespace ed
