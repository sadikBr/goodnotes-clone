// Pen gestures: scratching out ink with a quick back-and-forth scribble.
//
// It must never take ordinary writing for a scratch-out, so it asks a lot:
// a scribble is a stroke that goes back and forth over the same place many
// times (letters like m, w or a zigzag move on as they go, a scribble keeps
// coming back), and only ink it really covers goes (most of a stroke under
// the scribble; shading inside a drawing leaves the drawing). A scribble
// over nothing stays as ink.
#pragma once

#include <QSet>
#include <QString>
#include <optional>
#include <vector>

#include "Model.h"

namespace ed {

struct Scribble {
  /// The area it covers (its convex hull), on the page.
  std::vector<P2> hull;
  Rect box;
  /// Back-and-forth legs.
  int legs = 0;
};

/// The stroke as a scribble, or nothing when it's ordinary ink.
std::optional<Scribble> asScribble(const std::vector<Pt>& points);

/// The elements a scribble covers among `els` (strokes, shapes, typed text;
/// never pictures, graphs or tables).
QSet<QString> scratchedOut(const Scribble& s, const std::vector<const El*>& els);

}  // namespace ed
