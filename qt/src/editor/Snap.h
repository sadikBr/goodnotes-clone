// Alignment guides while moving (was src/editor/snap.ts and textAnchors in
// render.ts). Text's anchors come from the glyphs (baseline, x-height
// centre, ink edges), so text lines up the way it looks; anything else
// lines up by its box (edges, middles) with the page's centre and the other
// things on the page.
#pragma once

#include <QString>
#include <vector>

#include "Model.h"

namespace ed {

struct Anchor {
  char axis;  ///< 'x' or 'y'
  QString kind;  ///< baseline, mid, left, right, center (targets also: rule, rulemid, margin, pagecenter)
  double value;
  double from, to;  ///< extent along the other axis
};
using Target = Anchor;

struct Guide {
  char axis;
  double value, from, to;
  QString label;
  double labelX, labelY;
};

struct SnapResult {
  double dx, dy;
  std::vector<Guide> guides;
};
/// A proposed move adjusted so the nearest anchor within `threshold` lands on a target.
SnapResult snapMove(const std::vector<Anchor>& anchors, const std::vector<Target>& targets, double dx, double dy, double threshold);
/// Targets from the paper: ruled lines, the space between them, the margin and page centre.
std::vector<Target> paperTargets(const std::vector<double>& rules, double spanFrom, double spanTo, bool hasMargin, double margin, double pageW, double pageH);
/// A box's anchors (anything moved that isn't only text): its edges and middles.
/// Also the targets other elements offer.
std::vector<Anchor> boxAnchors(const Rect& r);
/// A text box's anchors from its rendered glyphs.
std::vector<Anchor> textAnchors(const El& el);

}  // namespace ed
